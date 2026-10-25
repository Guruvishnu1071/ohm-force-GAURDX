"""Forward newline-delimited Arduino JSON from USB serial to Node 3."""
from __future__ import annotations

import json
import os
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

import serial

PORT = os.environ.get("GAURDX_SERIAL_PORT", "COM3")
BAUD = int(os.environ.get("GAURDX_BAUD", "9600"))
API_URL = os.environ.get("GAURDX_API_URL", "http://127.0.0.1:5000/api/telemetry")


def main() -> None:
    print(f"Forwarding {PORT} at {BAUD} baud to {API_URL}; Ctrl+C to stop")
    with serial.Serial(PORT, BAUD, timeout=1) as device:
        while True:
            raw = device.readline()
            if not raw:
                continue
            try:
                payload = json.loads(raw.decode("utf-8").strip())
                if not isinstance(payload, dict):
                    raise ValueError("serial line must contain a JSON object")
                body = json.dumps(payload).encode("utf-8")
                req = Request(API_URL, data=body, headers={"Content-Type": "application/json"})
                with urlopen(req, timeout=5) as response:
                    print(f"Telemetry accepted ({response.status})")
            except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
                print(f"Ignoring invalid serial line: {error}")
            except HTTPError as error:
                print(f"Node 3 rejected telemetry ({error.code}): {error.read().decode('utf-8', 'replace')}")
            except (URLError, serial.SerialException, TimeoutError) as error:
                print(f"Forwarding error: {error}; retrying after 2 seconds")
                time.sleep(2)


if __name__ == "__main__":
    main()