#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ---------- RFID ----------
#define SS_PIN 10
#define RST_PIN 9

MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- SERVO ----------
#define SERVO_PIN 6
Servo doorServo;

// ---------- LCD ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- AUTHORIZED CARD ----------
byte authorizedUID[] = {0xB0, 0x1F, 0x13, 0x55};

void setup() {

  Serial.begin(9600);

  // RFID
  SPI.begin();
  rfid.PCD_Init();

  // Servo
  doorServo.attach(SERVO_PIN);
  doorServo.write(0);       // Door locked

  // LCD
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("RFID DOOR LOCK");
  lcd.setCursor(0, 1);
  lcd.print("Scan Your Card");

  delay(2000);

  lcd.clear();
  lcd.print("Scan Your Card");
}

void loop() {

  // Check whether a new RFID card is present
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Read RFID card
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Print UID to Serial Monitor
  Serial.print("Card UID: ");

  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }

  Serial.println();

  // ---------- CHECK UID ----------

  bool authorized = true;

  if (rfid.uid.size != 4) {
    authorized = false;
  }
  else {

    for (byte i = 0; i < 4; i++) {

      if (rfid.uid.uidByte[i] != authorizedUID[i]) {
        authorized = false;
        break;
      }

    }
  }

  // ---------- ACCESS GRANTED ----------

  if (authorized) {

    Serial.println("ACCESS GRANTED");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ACCESS GRANTED");

    lcd.setCursor(0, 1);
    lcd.print("DOOR OPENING");

    // Open door
    doorServo.write(90);

    delay(5000);

    // Close door
    doorServo.write(0);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DOOR LOCKED");

    delay(2000);

  }

  // ---------- ACCESS DENIED ----------

  else {

    Serial.println("ACCESS DENIED");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ACCESS DENIED");

    lcd.setCursor(0, 1);
    lcd.print("INVALID CARD");

    delay(3000);
  }

  // Return to standby screen
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan Your Card");

  // Stop RFID communication
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(500);
}