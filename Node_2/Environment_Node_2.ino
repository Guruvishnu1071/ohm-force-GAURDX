#include <DHT.h>
#include <RTClib.h>
#include <Wire.h>

// Nano map. MQ readings remain ADC counts until calibrated for the sensor.
constexpr uint8_t DHT_PIN = 2;
constexpr uint8_t FLAME_PIN = 3;       // Module output LOW means flame detected.
constexpr uint8_t PUMP_RELAY_PIN = 4;
constexpr uint8_t VENT_ENABLE_PIN = 5; // L298N ENA; remove ENA jumper for PWM.
constexpr uint8_t VENT_IN1_PIN = 6;
constexpr uint8_t VENT_IN2_PIN = 7;
constexpr uint8_t BUZZER_PIN = 8;
constexpr uint8_t GAS_PIN = A0;
constexpr uint8_t SOIL_PIN = A1;
constexpr uint16_t GAS_ALARM_ADC = 400;
constexpr uint8_t SOIL_DRY_PERCENT = 35;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint32_t PUMP_PULSE_MS = 3000;
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr bool SOIL_DRY_IS_LOW = true;

#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;
uint32_t lastSampleMs = 0;
uint32_t pumpStartedMs = 0;
bool rtcAvailable = false;
bool pumpActive = false;
bool ventActive = false;

void setPump(bool on) {
  pumpActive = on;
  digitalWrite(PUMP_RELAY_PIN, on == RELAY_ACTIVE_LOW ? LOW : HIGH);
  if (on) pumpStartedMs = millis();
}

void setVent(bool open) {
  ventActive = open;
  if (open) {
    digitalWrite(VENT_IN1_PIN, HIGH);
    digitalWrite(VENT_IN2_PIN, LOW);
    analogWrite(VENT_ENABLE_PIN, 220);
  } else {
    analogWrite(VENT_ENABLE_PIN, 0);
    digitalWrite(VENT_IN1_PIN, LOW);
    digitalWrite(VENT_IN2_PIN, LOW);
  }
}

void printTimestamp() {
  if (!rtcAvailable) { Serial.print(F("null")); return; }
  DateTime now = rtc.now();
  char timestamp[20];
  snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  Serial.print('"'); Serial.print(timestamp); Serial.print('"');
}

void setup() {
  Serial.begin(9600);
  dht.begin();
  rtcAvailable = rtc.begin();
  pinMode(FLAME_PIN, INPUT_PULLUP);
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(VENT_ENABLE_PIN, OUTPUT);
  pinMode(VENT_IN1_PIN, OUTPUT);
  pinMode(VENT_IN2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  setPump(false);
  setVent(false);
  digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
  uint32_t nowMs = millis();
  if (pumpActive && nowMs - pumpStartedMs >= PUMP_PULSE_MS) setPump(false);
  if (nowMs - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = nowMs;

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  uint16_t gasAdc = analogRead(GAS_PIN);
  uint16_t soilAdc = analogRead(SOIL_PIN);
  bool flameDetected = digitalRead(FLAME_PIN) == LOW;
  // Calibrate probe endpoints for the actual sensor; default assumes LOW is dry.
  int soilPercent = constrain(SOIL_DRY_IS_LOW ? map(soilAdc, 0, 1023, 0, 100)
                                               : map(soilAdc, 0, 1023, 100, 0), 0, 100);
  bool hazard = flameDetected || gasAdc >= GAS_ALARM_ADC;

  // Local hazard response has priority and cancels irrigation.
  if (hazard) {
    setPump(false);
    setVent(true);
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    setVent(false);
    digitalWrite(BUZZER_PIN, LOW);
    if (soilPercent < SOIL_DRY_PERCENT && !pumpActive) setPump(true);
  }

  Serial.print(F("{\"timestamp\":")); printTimestamp();
  Serial.print(F(",\"temperature_c\":"));
  if (isnan(temperature)) Serial.print(F("null")); else Serial.print(temperature, 1);
  Serial.print(F(",\"humidity_pct\":"));
  if (isnan(humidity)) Serial.print(F("null")); else Serial.print(humidity, 1);
  Serial.print(F(",\"gas_adc\":")); Serial.print(gasAdc);
  Serial.print(F(",\"flame_detected\":")); Serial.print(flameDetected ? F("true") : F("false"));
  Serial.print(F(",\"soil_moisture_pct\":")); Serial.print(soilPercent);
  Serial.print(F(",\"pump_active\":")); Serial.print(pumpActive ? F("true") : F("false"));
  Serial.print(F(",\"vent_status\":\"")); Serial.print(ventActive ? F("OPEN") : F("CLOSED"));
  Serial.print(F("\",\"hazard_detected\":")); Serial.print(hazard ? F("true") : F("false"));
  Serial.println('}');
}