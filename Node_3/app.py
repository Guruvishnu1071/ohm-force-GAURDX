"""Local GAURDX telemetry and access-log API (Node 3)."""
from __future__ import annotations

import csv
import os
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, request

app = Flask(__name__)
DATA_DIR = Path(os.environ.get("GAURDX_DATA_DIR", Path(__file__).with_name("data")))
TELEMETRY_FILE = DATA_DIR / "telemetry.csv"
ACCESS_FILE = DATA_DIR / "access_log.csv"
LOCK = threading.Lock()
TELEMETRY_FIELDS = ["received_at", "timestamp", "temperature_c", "humidity_pct", "gas_adc",
                    "flame_detected", "soil_moisture_pct", "pump_active", "vent_status", "hazard_detected"]
ACCESS_FIELDS = ["received_at", "uid", "status"]


def append_csv(path: Path, fields: list[str], row: dict[str, Any]) -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    with LOCK:
        new_file = not path.exists() or path.stat().st_size == 0
        with path.open("a", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
            if new_file:
                writer.writeheader()
            writer.writerow(row)


def read_recent(path: Path, limit: int) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with LOCK, path.open("r", newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))[-limit:]


def valid_number(value: Any, name: str, minimum: float, maximum: float) -> bool:
    if value is None:
        return name in {"temperature_c", "humidity_pct"}
    return (not isinstance(value, bool) and isinstance(value, (int, float))
            and minimum <= value <= maximum)


@app.get("/")
def index():
    return jsonify({"service": "GAURDX Node 3", "status": "ok",
                    "endpoints": ["/health", "/api/telemetry", "/api/access-log"]})


@app.get("/health")
def health():
    return jsonify({"status": "ok"})


@app.post("/api/telemetry")
def ingest_telemetry():
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify({"error": "request body must be a JSON object"}), 400
    required = {"temperature_c", "humidity_pct", "gas_adc", "flame_detected",
                "soil_moisture_pct", "pump_active", "vent_status"}
    missing = sorted(required - payload.keys())
    if missing:
        return jsonify({"error": "missing required fields", "fields": missing}), 400
    ranges = {"temperature_c": (-40, 80), "humidity_pct": (0, 100), "gas_adc": (0, 1023),
              "soil_moisture_pct": (0, 100)}
    for name, (low, high) in ranges.items():
        if not valid_number(payload[name], name, low, high):
            return jsonify({"error": f"{name} must be between {low} and {high}"}), 400
    for name in ("flame_detected", "pump_active"):
        if not isinstance(payload[name], bool):
            return jsonify({"error": f"{name} must be boolean"}), 400
    if payload["vent_status"] not in {"OPEN", "CLOSED"}:
        return jsonify({"error": "vent_status must be OPEN or CLOSED"}), 400
    if "hazard_detected" in payload and not isinstance(payload["hazard_detected"], bool):
        return jsonify({"error": "hazard_detected must be boolean"}), 400
    row = {key: payload.get(key, "") for key in TELEMETRY_FIELDS}
    row["received_at"] = datetime.now(timezone.utc).isoformat()
    append_csv(TELEMETRY_FILE, TELEMETRY_FIELDS, row)
    return jsonify({"status": "accepted", "received_at": row["received_at"]}), 201


@app.get("/api/telemetry")
def list_telemetry():
    try:
        limit = max(1, min(int(request.args.get("limit", "100")), 1000))
    except ValueError:
        return jsonify({"error": "limit must be an integer"}), 400
    return jsonify(read_recent(TELEMETRY_FILE, limit))


@app.route("/api/access-log", methods=["GET", "POST"])
def access_log():
    # GET preserves Node 1's documented query format; POST accepts JSON callers too.
    if request.method == "GET":
        uid = request.args.get("uid", "").strip().upper()
        status = request.args.get("status", "").strip().upper()
    else:
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            return jsonify({"error": "request body must be a JSON object"}), 400
        uid = str(payload.get("uid", "")).strip().upper()
        status = str(payload.get("status", "")).strip().upper()
    if not uid or len(uid) > 32 or not all(char in "0123456789ABCDEF" for char in uid):
        return jsonify({"error": "uid must be a hexadecimal string of 1 to 32 characters"}), 400
    if status not in {"SUCCESS", "FAILURE", "DENIED"}:
        return jsonify({"error": "status must be SUCCESS, FAILURE, or DENIED"}), 400
    row = {"received_at": datetime.now(timezone.utc).isoformat(), "uid": uid, "status": status}
    append_csv(ACCESS_FILE, ACCESS_FIELDS, row)
    return jsonify({"status": "logged", **row}), 201


if __name__ == "__main__":
    app.run(host=os.environ.get("GAURDX_HOST", "127.0.0.1"),
            port=int(os.environ.get("GAURDX_PORT", "5000")), debug=False)
