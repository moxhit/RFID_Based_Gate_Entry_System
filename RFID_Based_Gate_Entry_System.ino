#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <BearSSLHelpers.h>
#include <CertStoreBearSSL.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// Define the SS and RST pins for the MFRC522 module
#define SS_PIN D4
#define RST_PIN D3

// Set the interval for scanning RFID cards in milliseconds
#define SCAN_INTERVAL 100

// Your WiFi credentials
const char* ssid = "Your_SSID";
const char* password = "Password";

// SHA-256 fingerprint of the server's SSL certificate
const char* fingerprint = "Website's Fingerprint";
const char* expectedFingerprint = "Website's Fingerprint"; // Update this when certificate changes

String device_id = "Your_Device_string_id";
String mac_id = "";

// HTTPS server details
const char* serverURL = "Your_Server_url";
const int serverPort = 443; // Default port for HTTPS is 443

// API endpoint
const char* serverPath = "Your_API";

// Create an instance of the MFRC522 library
MFRC522 mfrc522(SS_PIN, RST_PIN);

void resetReader() {
  mfrc522.PCD_Init();
}

// Function to connect to WiFi
void connectToWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi!");
  String mac = WiFi.macAddress();
  mac.toLowerCase();
  mac.replace(":", "");  // Remove colons from the MAC address
  mac_id = mac;
  delay(1000);
  resetReader();
}

// Function to verify SSL fingerprint
bool verifyFingerprint(BearSSL::WiFiClientSecure& client) {
  client.setFingerprint(fingerprint);
  return true; // Assume fingerprint verification succeeds (simplified)
}

// Function to send data to the HTTPS server
String sendDataToServer(String data) {
  BearSSL::WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification (optional, remove if using verified certificates)
  client.setTimeout(50); // Increase the timeout to 5 seconds for debugging

  // Verify fingerprint before connecting
  if (!verifyFingerprint(client)) {
    return "Fingerprint verification failed";
  }

  if (client.connect(serverURL, serverPort)) {
    String payload = "{\"rfId\":\"" + data + "\", \"deviceId\":\"" + device_id + "\", \"macId\":\"" + mac_id + "\"}";

    Serial.println("Connected to server, sending request...");

    HTTPClient https;
    https.begin(client, serverURL, serverPort, serverPath);
    https.addHeader("Content-Type", "application/json");

    int httpResponseCode = https.POST(payload);

    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);

      String response = https.getString();
      Serial.print("Server response: ");
      Serial.println(response);

      https.end();
      client.stop();
      return response;
    } else {
      Serial.print("HTTP Error code: ");
      Serial.println(httpResponseCode);

      https.end();
      client.stop();
      return "HTTP request failed";
    }
  } else {
    Serial.println("Failed to connect to server.");
    return "Connection failed";
  }
}

void setup() {
  pinMode(D1, OUTPUT); // Green LED
  pinMode(D2, OUTPUT); // Red LED
  pinMode(D8, OUTPUT); // Buzzer
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  resetReader();
  connectToWiFi();
}

void loop() {
  static bool blinkLed = false;
  static unsigned long blinkStartTime = 0;
  static unsigned long lastBlinkTime = 0;
  static bool greenLedState = false;
  static bool redLedState = false;
  static bool toggleState = false;

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String rfidData = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      rfidData += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      rfidData += String(mfrc522.uid.uidByte[i], DEC);
    }
    rfidData.toUpperCase();

    Serial.println("RFID Data: " + rfidData);

    // Beep the buzzer when RFID card is detected
    digitalWrite(D8, HIGH);
    delay(100); // Beep duration
    digitalWrite(D8, LOW);

    String serverResponse = sendDataToServer(rfidData); // Get response from the server
    Serial.println("Server response: " + serverResponse);

    // Clear the UID buffer to prepare for the next card
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    // Parse JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, serverResponse);

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    const char* statusType = doc["statusType"];
    Serial.println(statusType);

    if (strcmp(statusType, "error") == 0) {
      blinkLed = true;
      greenLedState = false;
      redLedState = true;
      Serial.println("Reject");
      blinkStartTime = millis(); // Record the time the blinking started
      lastBlinkTime = millis(); // Initialize the last blink time
      toggleState = false; // Initialize toggle state
    } else if (strcmp(statusType, "in") == 0 || strcmp(statusType, "out") == 0) {
      blinkLed = true;
      greenLedState = true;
      redLedState = false;
      Serial.println("Approve");
      digitalWrite(D1, HIGH); // Turn on green LED
      digitalWrite(D2, LOW); // Ensure red LED is off
      digitalWrite(D8, HIGH); // Turn on buzzer for green LED
      blinkStartTime = millis(); // Record the time the green LED was turned on
    } else {
      blinkLed = true;
      greenLedState = false;
      redLedState = true;
      Serial.println("Unknown response");
      blinkStartTime = millis(); // Record the time the blinking started
      lastBlinkTime = millis(); // Initialize the last blink time
      toggleState = false; // Initialize toggle state
    }
  }

  unsigned long currentTime = millis();
  if (greenLedState) {
    if (currentTime - blinkStartTime > 2000) { // Keep the green LED on for 2 seconds
      digitalWrite(D1, LOW); // Turn off green LED
      digitalWrite(D8, LOW); // Turn off buzzer
      greenLedState = false;
    }
  }

  if (redLedState) {
    if (currentTime - blinkStartTime <= 2000) { // Blink for 3 seconds
      if (currentTime - lastBlinkTime >= 100) { // Toggle every 100 milliseconds
        lastBlinkTime = currentTime; // Update the last blink time
        toggleState = !toggleState; // Toggle state
        digitalWrite(D2, toggleState ? HIGH : LOW); // Toggle red LED
        digitalWrite(D8, toggleState ? HIGH : LOW); // Toggle buzzer
      }
    } else {
      // Turn off the red LED and buzzer after 3 seconds
      digitalWrite(D2, LOW);
      digitalWrite(D8, LOW);
      redLedState = false;
    }
  }

  // Wait for the specified interval before scanning for the next card
  delay(SCAN_INTERVAL);
  resetReader();
}
