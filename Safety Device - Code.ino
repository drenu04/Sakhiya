#include <TinyGPS++.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// === Pins ===
#define BUTTON_PIN     18
#define PIEZO_PIN      34
#define LED_PIN        2

// === WiFi & Telegram Credentials ===
const char* ssid = "Project";
const char* password = "12345678";
const char* botToken = "7684657386:AAH5VYxbHwYMuPdEyFBhciQl9bYOYbAClH8";
const char* chatID = "1769923214";
const char* mobileNumber = "+919121612398";

// === GPS & SIM800L Serial Setup ===
HardwareSerial gpsSerial(1);   // RX=4, TX=15
HardwareSerial sim800(2);      // RX=16, TX=17
TinyGPSPlus gps;

// === Telegram Bot ===
WiFiClientSecure secured_client;
UniversalTelegramBot bot(botToken, secured_client);

// === Timing & Flags ===
unsigned long lastTime = 0;
const int delayTime = 1000;

bool waitingForStop = false;
unsigned long piezoTriggerTime = 0;
bool stopCommandReceived = false;
bool wifiConnected = false;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Booting...");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  gpsSerial.begin(9600, SERIAL_8N1, 4, 15);   // GPS: RX=4, TX=15
  sim800.begin(9600, SERIAL_8N1, 17, 16);     // SIM800L: RX=16, TX=17

  // === Connect to WiFi (with timeout)
  Serial.print("Connecting to WiFi: ");
  WiFi.begin(ssid, password);
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    secured_client.setInsecure();
    bot.sendMessage(chatID, "🚨 Device Initialized!\nSend /location to get current GPS location.", "");
    Serial.println("Telegram: Startup message sent.");
  } else {
    wifiConnected = false;
    Serial.println("\n❌ WiFi NOT connected! Telegram features disabled.");
  }
}

void loop() {
  // === Read GPS
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // === Button Pressed → Send SMS with Location
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(300); // debounce
    Serial.println("🔘 Button pressed: Sending SMS...");

    if (gps.location.isValid()) {
      float lat = gps.location.lat();
      float lng = gps.location.lng();
      String locLink = "🚨 Button Pressed!\n📍 Location:\nhttps://www.google.com/maps?q=" + String(lat, 6) + "," + String(lng, 6);
      sendSMS(locLink);
      Serial.println("📤 SMS Sent: " + locLink);
    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    digitalWrite(LED_PIN, LOW);
    delay(500);
    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    digitalWrite(LED_PIN, LOW);
    } else {
      sendSMS("❌ GPS location not available.");
      Serial.println("⚠️ GPS not available.");
    }
    delay(1000);
  }

  // === Piezo Triggered
  int piezoVal = analogRead(PIEZO_PIN);
  if (piezoVal > 500 && !waitingForStop) {
    Serial.println("🎯 Piezo Triggered: LED On & waiting for STOP...");
    digitalWrite(LED_PIN, HIGH);
    delay(2000);
    digitalWrite(LED_PIN, LOW);
    delay(1000);
    digitalWrite(LED_PIN, HIGH);
    delay(2000);
    digitalWrite(LED_PIN, LOW);

    waitingForStop = true;
    piezoTriggerTime = millis();
    stopCommandReceived = false;

    if (wifiConnected) {
      bot.sendMessage(chatID, "🚨 Piezo Triggered!\nReply with /stop within 10 seconds to cancel the call.", "");
    }
  }

  // === Check for STOP command timeout
  if (waitingForStop && (millis() - piezoTriggerTime > 10000)) {
    waitingForStop = false;

    if (stopCommandReceived) {
      Serial.println("✅ Stop command received. Call canceled.");
      if (wifiConnected) {
        bot.sendMessage(chatID, "✅ Call canceled by user.", "");
      }
    } else {
      Serial.println("⏰ No stop received. Making call...");
      makeCall();
      if (wifiConnected) {
        bot.sendMessage(chatID, "📞 No response. Calling emergency number...", "");
      }
    }
  }

  // === Telegram Bot Check (only if WiFi is connected)
  if (wifiConnected && millis() - lastTime > delayTime) {
    int msgCount = bot.getUpdates(bot.last_message_received + 1);
    while (msgCount) {
      Serial.println("📩 Telegram: New message");

      for (int i = 0; i < msgCount; i++) {
        String text = bot.messages[i].text;
        String userId = bot.messages[i].chat_id;
        Serial.println("User sent: " + text);

        if (text == "/stop" && waitingForStop) {
          stopCommandReceived = true;
          waitingForStop = false;
          bot.sendMessage(userId, "🛑 Stop command received. Emergency call canceled.", "");
          continue;
        }

        if (text == "/location") {
          if (gps.location.isValid()) {
            float lat = gps.location.lat();
            float lng = gps.location.lng();
            String msg = "📍 Location:\nhttps://www.google.com/maps?q=" + String(lat, 6) + "," + String(lng, 6);
            bot.sendMessage(userId, msg, "");
            Serial.println("Telegram: Location sent.");
          } else {
            bot.sendMessage(userId, "❌ GPS location not available.", "");
            Serial.println("Telegram: Location not available.");
          }
        } else {
          bot.sendMessage(userId, "Available commands:\n/location\n/stop", "");
          Serial.println("Telegram: Sent command options.");
        }
      }

      msgCount = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTime = millis();
  }
}

// === Send SMS via SIM800L
void sendSMS(String text) {
  sim800.println("AT+CMGF=1");
  delay(300);
  sim800.print("AT+CMGS=\"");
  sim800.print(mobileNumber);
  sim800.println("\"");
  delay(300);
  sim800.print(text);
  delay(300);
  sim800.write(26);  // Ctrl+Z
  delay(1000);
}

// === Make a Call via SIM800L
void makeCall() {
  sim800.print("ATD");
  sim800.print(mobileNumber);
  sim800.println(";");
  delay(20000); // Call duration
  sim800.println("ATH"); // Hang up
  Serial.println("📞 Call made and ended.");
}
