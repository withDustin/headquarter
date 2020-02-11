#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <MFRC522.h>
#include <SPI.h>

#define LED_ON LOW
#define LED_OFF HIGH
#define OPEN_TIMEOUT 3000

#define led 13
#define relay 38

#define RST_PIN 49
#define SS_PIN 53
MFRC522 mfrc522(SS_PIN, RST_PIN);

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);  // (rs, enable, d4, d5, d6, d7)

bool programMode = false;  // initialize programming mode to false

uint8_t successRead;

byte readCard[4];
byte storedCard[4];  // Stores an ID read from EEPROM
byte masterCard[4] = {171, 201, 130, 34};

void setup() {
  // Arduino Pin configuration
  pinMode(relay, OUTPUT);

  digitalWrite(relay, HIGH);

  Serial.begin(9600);
  // while (!Serial)
  //   ;

  SPI.begin();
  mfrc522.PCD_Init();
  lcd.begin(16, 2);

  lcdPrintDefaultMessage();

  Serial.println("Ready!");
}

void loop() {
  lcdPrintDefaultMessage();
  do {
    successRead = getID();  // sets successRead to 1 when we get read from
                            // reader otherwise 0
  } while (!successRead);   // Program will not go further while you not get a
                            // successful read

  if (programMode) {
    if (isMaster(readCard)) {
      programMode = false;
      lcd.clear();
      lcd.print("Goodbye Master!");
      delay(1000);
    } else {
      if (findID(readCard)) {
        // if card exists, delete it
        deleteID(readCard);
      } else {
        addID(readCard);
      }
    }
  } else {
    if (isMaster(readCard)) {
      programMode = true;
      lcd.clear();
      lcd.print("Entering Master mode");
      delay(1000);
    } else {
      if (findID(readCard)) {  // If not, see if the card is in the EEPROM
        Serial.println(F("Welcome, You shall pass"));
        granted();  // Open the door lock for 300 ms
      } else {      // If not, show that the ID was not valid
        Serial.println(F("You shall not pass"));
        denied();
      }
    }
  }
}

//* Access Granted
void granted() {
  lcd.clear();
  lcd.write("   Welcome to");
  lcd.setCursor(0, 1);
  lcd.write("     Targeek");

  digitalWrite(relay, LOW);   // Unlock door!
  delay(OPEN_TIMEOUT);        // Hold door lock open for given seconds
  digitalWrite(relay, HIGH);  // Relock door
}

void denied() {
  lcd.clear();
  lcd.write("Who are you?");
  delay(1000);
}

//* Get PICC's UID
uint8_t getID() {
  // Getting ready for Reading PICCs
  if (!mfrc522.PICC_IsNewCardPresent()) {  // If a new PICC placed to RFID
                                           // reader continue
    return 0;
  }
  if (!mfrc522.PICC_ReadCardSerial()) {  // Since a PICC placed get Serial and
                                         // continue
    return 0;
  }
  // There are Mifare PICCs which have 4 byte or 7 byte UID care if you use 7
  // byte PICC I think we should assume every PICC as they have 4 byte UID Until
  // we support 7 byte PICCs
  Serial.print(F("Scanned PICC's UID: "));
  for (uint8_t i = 0; i < 4; i++) {  //
    readCard[i] = mfrc522.uid.uidByte[i];
    Serial.print(readCard[i]);
    Serial.print(" ");
  }
  Serial.println("");
  mfrc522.PICC_HaltA();  // Stop reading
  return 1;
}

//* Check bytes
bool checkTwo(byte a[], byte b[]) {
  for (uint8_t k = 0; k < 4; k++) {  // Loop 4 times
    if (a[k] != b[k]) {  // IF a != b then false, because: one fails, all fail
      return false;
    }
  }
  return true;
}

void lcdPrintDefaultMessage() {
  lcd.clear();

  if (programMode) {
    lcd.print("  Master mode");
    lcd.setCursor(0, 1);
    lcd.print("waiting for card");
    return;
  }

  lcd.print(" Scan to enter");
  lcd.setCursor(0, 1);
  lcd.print("v0.0.1 @ Targeek");
}

bool isMaster(byte test[]) {
  bool master = checkTwo(test, masterCard);
  if (!master) return false;
  Serial.println("Master!!!");
  return true;
}

void readID(uint8_t number) {
  uint8_t start = (number * 4) + 2;  // Figure out starting position
  for (uint8_t i = 0; i < 4; i++) {  // Loop 4 times to get the 4 Bytes
    storedCard[i] =
        EEPROM.read(start + i);  // Assign values read from EEPROM to array
  }
}

bool findID(byte find[]) {
  uint8_t count = EEPROM.read(0);        // Read the first Byte of EEPROM that
  for (uint8_t i = 1; i < count; i++) {  // Loop once for each EEPROM entry
    readID(i);  // Read an ID from EEPROM, it is stored in storedCard[4]
    if (checkTwo(
            find,
            storedCard)) {  // Check to see if the storedCard read from EEPROM
      return true;
    } else {  // If not, return false
    }
  }
  return false;
}

void addID(byte a[]) {
  if (!findID(a)) {  // Before we write to the EEPROM, check to see if we have
                     // seen this card before!
    uint8_t num = EEPROM.read(0);   // Get the numer of used spaces, position 0
                                    // stores the number of ID cards
    uint8_t start = (num * 4) + 6;  // Figure out where the next slot starts
    num++;                          // Increment the counter by one
    EEPROM.write(0, num);           // Write the new count to the counter
    for (uint8_t j = 0; j < 4; j++) {  // Loop 4 times
      EEPROM.write(
          start + j,
          a[j]);  // Write the array values to EEPROM in the right position
    }
    successWrite();
    Serial.println(F("Succesfully added ID record to EEPROM"));
  } else {
    failedWrite();
    Serial.println(F("Failed! There is something wrong with ID or bad EEPROM"));
  }
}

void deleteID(byte a[]) {
  if (!findID(a)) {  // Before we delete from the EEPROM, check to see if we
                     // have this card!
    failedWrite();   // If not
    Serial.println(F("Failed! There is something wrong with ID or bad EEPROM"));
  } else {
    uint8_t num = EEPROM.read(0);  // Get the numer of used spaces, position 0
                                   // stores the number of ID cards
    uint8_t slot;                  // Figure out the slot number of the card
    uint8_t
        start;  // = ( num * 4 ) + 6; // Figure out where the next slot starts
    uint8_t looping;  // The number of times the loop repeats
    uint8_t j;
    uint8_t count = EEPROM.read(
        0);  // Read the first Byte of EEPROM that stores number of cards
    slot = findIDSLOT(a);  // Figure out the slot number of the card to delete
    start = (slot * 4) + 2;
    looping = ((num - slot) * 4);
    num--;                           // Decrement the counter by one
    EEPROM.write(0, num);            // Write the new count to the counter
    for (j = 0; j < looping; j++) {  // Loop the card shift times
      EEPROM.write(
          start + j,
          EEPROM.read(
              start + 4 +
              j));  // Shift the array values to 4 places earlier in the EEPROM
    }
    for (uint8_t k = 0; k < 4; k++) {  // Shifting loop
      EEPROM.write(start + j + k, 0);
    }
    successDelete();
    Serial.println(F("Succesfully removed ID record from EEPROM"));
  }
}

void successDelete() {
  lcd.clear();
  lcd.write("Card removed");
  delay(1000);
}

void successWrite() {
  lcd.clear();
  lcd.write("Card added!");
  delay(1000);
}

void failedWrite() {
  lcd.clear();
  lcd.write("Write failed");
  delay(1000);
}

uint8_t findIDSLOT(byte find[]) {
  uint8_t count = EEPROM.read(0);         // Read the first Byte of EEPROM that
  for (uint8_t i = 1; i <= count; i++) {  // Loop once for each EEPROM entry
    readID(i);  // Read an ID from EEPROM, it is stored in storedCard[4]
    if (checkTwo(
            find,
            storedCard)) {  // Check to see if the storedCard read from EEPROM
      // is the same as the find[] ID card passed
      return i;  // The slot number of the card
    }
  }
}