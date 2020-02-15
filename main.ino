#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <MFRC522.h>
#include <SPI.h>
#include <pins_arduino.h>

#define VERSION "v0.1"

WiFiServer server(WEB_SERVER_PORT);
MFRC522 mfrc522(SS_PIN, RST_PIN);

bool masterMode = false;
uint8_t successRead;
byte storedCard[4];
byte readCard[4];
byte masterCard[4];
String header;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

void (* resetFunc) (void) = 0;

void setup() {
  digitalWrite(RST_PIN, HIGH);
  digitalWrite(door, HIGH);

  // Arduino pin config
  pinMode(door, OUTPUT);
  pinMode(led, OUTPUT);
  pinMode(RST_PIN, OUTPUT);

  digitalWrite(led, LOW);
  digitalWrite(bell, LOW);

  EEPROM.begin(512);
  Serial.begin(SERIAL_PORT);
  SPI.begin();

  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();

  closeTheDoor();

  blinkLed(2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting WiFi");
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(DELAY_500);
  //   blinkLed(1);
  //   Serial.print(".");
  // }

  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();

  if (EEPROM.read(1) != 144) {
    Serial.println(F("No Master Card Defined"));
    Serial.println(F("Scan A PICC to Define as Master Card"));
    do {
      successRead = getID();
      // https://forum.arduino.cc/index.php?topic=442570.msg3046667#msg3046667
      yield();
    } while (!successRead);

    for (uint8_t j = 0; j < 4; j++) {
      EEPROM.write(2 + j, readCard[j]);
      Serial.print(readCard[j], HEX);
    }
    Serial.println("");
    EEPROM.write(1, 144);
    Serial.println(F("Master Card Defined"));
  }

  Serial.println(F("-------------------"));
  Serial.println(F("Master Card's UID"));
  for (uint8_t i = 0; i < 4; i++) {      // Read Master Card's UID from EEPROM
    masterCard[i] = EEPROM.read(2 + i);  // Write it to masterCard
  }
  EEPROM.commit();

  Serial.println("");
  Serial.println(F("-------------------"));
  Serial.println(F("Everything is ready"));
  Serial.println(F("Waiting PICCs to be scanned"));

  blinkLed(2);
}

void loop() {
  do {
    handleWebClient();
    digitalWrite(bell, LOW);
    // mfrc522.PCD_DumpVersionToSerial();
    // delay(500);
    if (masterMode) {
      blinkLed(1);
    } else {
      digitalWrite(led, HIGH);
      ledBlinkHeartbeat();
    }
    successRead = getID();
    yield();
  } while (!successRead);

  if (masterMode) {
    if (isMaster(readCard)) {
      Serial.println(F("Master Card Scanned"));
      Serial.println(F("Exiting Master Mode"));
      Serial.println(F("-----------------------------"));
      masterMode = false;
      return;
    } else {
      if (findID(readCard)) {
        Serial.println(F("I know this PICC, removing..."));
        removeID(readCard);
        Serial.println("-----------------------------");
        Serial.println(F("Scan a PICC to ADD or REMOVE"));
      } else {  // If scanned card is not known add it
        Serial.println(F("I do not know this PICC, adding..."));
        addID(readCard);
        Serial.println(F("-----------------------------"));
        Serial.println(F("Scan a PICC to ADD or REMOVE"));
      }
    }
  } else {
    if (isMaster(readCard)) {
      masterMode = true;
      Serial.println(F("Hello Master - Entered Master Mode"));
      uint8_t count = EEPROM.read(0);
      Serial.print(F("I have "));
      Serial.print(count);
      Serial.print(F(" record(s) on EEPROM"));
      Serial.println("");
      Serial.println(F("Scan a PICC to ADD or REMOVE to EEPROM"));
      Serial.println(F("Scan Master Card again to Exit Master Mode"));
      Serial.println(F("-----------------------------"));
    } else {
      if (findID(readCard)) {
        Serial.println(F("Welcome, You shall pass"));
        openTheDoor();
      } else {
        Serial.println(F("You shall not pass"));
        for (uint8_t i = 0; i < 5; ++i) {
          digitalWrite(led, LOW);
          digitalWrite(bell, LOW);
          delay(100);
          digitalWrite(led, HIGH);
          digitalWrite(bell, HIGH);
          delay(100);
        }
        digitalWrite(bell, LOW);
      }
    }
  }

  EEPROM.commit();
}

void handleWebClient() {
  WiFiClient client = server.available();

  if (client) {                     // If a new client connects,
    Serial.println("New Client.");  // print a message out in the serial port
    String currentLine =
        "";  // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() &&
           currentTime - previousTime <=
               timeoutTime) {  // loop while the client's connected
      currentTime = millis();
      if (client.available()) {  // if there's bytes to read from the client,
        char c = client.read();  // read a byte, then
        Serial.write(c);         // print it out the serial monitor
        header += c;
        if (c == '\n') {  // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a
          // row. that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200
            // OK) and a content-type so the client knows what's coming, then a
            // blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("OK");

            client.stop();

            if (header.indexOf("GET /open") >= 0 ||
                header.indexOf("POST /open") >= 0) {
              Serial.println("open request");
              openTheDoor();
            }
            if (header.indexOf("GET /reset") >= 0 ||
                header.indexOf("POST /reset") >= 0) {
              Serial.println("reset request");
              resetFunc();
            }

            break;
          } else {  // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage
                                 // return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
  }
}

uint8_t getID() {
  // Getting ready for Reading PICCs
  if (!mfrc522.PICC_IsNewCardPresent()) {  // If a new PICC placed to RFID
                                           // reader continue
    // Serial.println("PICC_IsNewCardPresent");

    return 0;
  }
  if (!mfrc522.PICC_ReadCardSerial()) {  // Since a PICC placed get Serial and
    // continue
    Serial.println("PICC_ReadCardSerial");
    return 0;
  }
  Serial.println(F("Scanned PICC's UID:"));
  for (uint8_t i = 0; i < 4; i++) {  //
    readCard[i] = mfrc522.uid.uidByte[i];
    Serial.print(readCard[i], HEX);
  }
  Serial.println("");
  mfrc522.PICC_HaltA();  // Stop reading
  return 1;
}

bool isMaster(byte test[]) {
  bool master = compareIDs(test, masterCard);

  if (master) {
    Serial.println("Master");
  }
  return master;
}

bool compareIDs(byte a[], byte b[]) {
  for (uint8_t k = 0; k < 4; k++) {  // Loop 4 times
    if (a[k] != b[k]) {  // IF a != b then false, because: one fails, all fail
      return false;
    }
  }
  return true;
}

void readID(uint8_t number) {
  uint8_t start = (number * 4) + 2;  // Figure out starting position
  for (uint8_t i = 0; i < 4; i++) {  // Loop 4 times to get the 4 Bytes
    storedCard[i] =
        EEPROM.read(start + i);  // Assign values read from EEPROM to array
  }
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
    Serial.println(F("Succesfully added ID record to EEPROM"));

  } else {
    // failedWrite();
    Serial.println(F("Failed! There is something wrong with ID or bad EEPROM"));
  }

  delay(DELAY_1);
}

bool findID(byte find[]) {
  uint8_t count = EEPROM.read(0);        // Read the first Byte of EEPROM that
  for (uint8_t i = 1; i < count; i++) {  // Loop once for each EEPROM entry
    readID(i);  // Read an ID from EEPROM, it is stored in storedCard[4]
    if (compareIDs(
            find,
            storedCard)) {  // Check to see if the storedCard read from EEPROM
      return true;
    } else {  // If not, return false
    }
  }
  return false;
}

uint8_t findIDSlot(byte find[]) {
  uint8_t count = EEPROM.read(0);         // Read the first Byte of EEPROM that
  for (uint8_t i = 1; i <= count; i++) {  // Loop once for each EEPROM entry
    readID(i);  // Read an ID from EEPROM, it is stored in storedCard[4]
    if (compareIDs(
            find,
            storedCard)) {  // Check to see if the storedCard read from EEPROM
      // is the same as the find[] ID card passed
      return i;  // The slot number of the card
    }
  }
}

void removeID(byte a[]) {
  if (!findID(a)) {  // Before we delete from the EEPROM, check to see if we
                     // have this card!
    // failedWrite();      // If not
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
    slot = findIDSlot(a);  // Figure out the slot number of the card to delete
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
    // successDelete();
    Serial.println(F("Succesfully removed ID record from EEPROM"));
  }
  delay(DELAY_1);
}

void closeTheDoor() {
  digitalWrite(door, DOOR_CLOSE);
  Serial.println("door closed");
}

void openTheDoor() {
  blinkLed(1);

  digitalWrite(bell, HIGH);
  digitalWrite(led, LOW);
  digitalWrite(door, DOOR_OPEN);

  delay(100);
  digitalWrite(bell, LOW);

  Serial.print("door opened by ID ");
  for (uint8_t i = 0; i < 4; ++i) {
    Serial.print(readCard[i], HEX);
  }

  Serial.println("");

  resetFunc();

  delay(DOOR_OPEN_TIMEOUT);
  closeTheDoor();
}

char* cardToStr(byte card[]) {
  char* str = "";
  for (uint8_t i = 0; i < 4; ++i) {
    str[i] = (char)card[i];
  }
  return str;
}

void blinkLed(int times) {
  for (uint8_t i = 0; i < times; ++i) {
    digitalWrite(led, LOW);
    delay(100);
    digitalWrite(led, HIGH);
    delay(100);
  }
}

void ledBlinkHeartbeat() {
  int now = millis();
  if (((now / 100) % 600) == 0) { // 60 second
    resetFunc();
  }
  if (((now / 100) % 50) == 0) { // 5 second
    blinkLed(1);
  }
}
