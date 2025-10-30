#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ----- WIFI SETUP ----- //
const char* WIFI_SSID = "phone";         // set your SSID
const char* WIFI_PASSWORD = "googlehome"; // set your password

// Build a unique topic from MAC address: ce365-lab4-<mac-without-colons> SCRAPPED!!!
// topic is Lab4_Team9_Push. This is unique enough
String makeUniqueTopic() {
  // String mac = WiFi.macAddress();
  // mac.replace(":", "");
  // mac.toLowerCase();
  // return String("ce365-lab4-") + mac;
  return "Lab4_Team9_Push";
}

// Send a plain text POST to ntfy.sh/<topic>
bool sendNtfy(const String& topic, const String& title, const String& message) {
  String url = String("https://ntfy.sh/") + topic;
  Serial.println("ntfy: POST to " + url);

  WiFiClientSecure* client = new WiFiClientSecure();
  client->setInsecure(); // acceptable for short lab/DIY use; replace with cert verification for production

  HTTPClient http;
  bool okBegin = http.begin(*client, url);
  if (!okBegin) {
    Serial.println("ntfy: http.begin failed");
    delete client;
    return false;
  }

  // Set headers supported by ntfy: Title sets the notification title
  http.addHeader("Title", title);
  http.addHeader("Content-Type", "text/plain");

  int code = http.POST(message);
  String payload = http.getString();
  Serial.printf("ntfy: HTTP code=%d payload_len=%d\n", code, (int)payload.length());
  if (payload.length()) Serial.println(payload);

  http.end();
  delete client;

  return (code >= 200 && code < 300);
}

// connect to wifi
void connectWiFi() {
  Serial.print("Connecting to WiFi ");
  Serial.print(WIFI_SSID);
  Serial.print(" ...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  connectWiFi();

  String topic = makeUniqueTopic();
  Serial.println("ntfy topic: " + topic);

  String title = "ESP32 Notification";
  String message = "Hello from ESP32! Topic: " + topic;

  bool sent = sendNtfy(topic, title, message);
  if (sent) {
    Serial.println("Notification sent successfully.");
  } else {
    Serial.println("Notification failed.");
  }
}

void loop() {
  // nothing more to do; notification sent in setup
  delay(1000);
}
