#include <WiFi.h>

// WiFi network name and password:
const char * networkName = "phone";
const char * networkPswd = "googlehome";

// IP address of the PC (receiver) to send UDP data to
// Change this to your PC's IP on the same network
const char * udpAddress = "172.20.10.4";
const int udpPort = 3333;

//Are we currently connected?
boolean connected = false;

//The udp library class
WiFiUDP udp;

// Sequence number for packets
volatile uint32_t packetSeq = 0;

// Send interval in milliseconds
const uint32_t sendIntervalMs = 50;
uint32_t lastSendMs = 0;

void setup(){
  // Initilize hardware serial:
  Serial.begin(115200);
  
  //Connect to the WiFi network
  connectToWiFi(networkName, networkPswd);
}

void loop(){
  if (!connected) {
    delay(50);
    return;
  }

  uint32_t nowMs = millis();
  if (nowMs - lastSendMs >= sendIntervalMs) {
    lastSendMs = nowMs;
    uint32_t seq = packetSeq++;
    // Compose a short message with sequence and sender time
    // Example: SEQ=123,T=456789
    udp.beginPacket(udpAddress, udpPort);
    udp.printf("SEQ=%lu,T=%lu", (unsigned long)seq, (unsigned long)nowMs);
    int sent = udp.endPacket();
    Serial.printf("Sent SEQ=%lu to %s:%d (%s)\n", (unsigned long)seq, udpAddress, udpPort, sent == 1 ? "ok" : "fail");
  }
//   Serial.println("Sent packet!");
}

// connect to wifi
void connectToWiFi(const char * ssid, const char * pwd){
  Serial.println("Connecting to WiFi network: " + String(ssid));

  // delete old config
  WiFi.disconnect(true);
  // register event handlers (ESP-IDF style events)
  WiFi.onEvent(WiFiEvent);
  
  //Initiate connection
  WiFi.begin(ssid, pwd);

  Serial.println("Waiting for WIFI connection...");
}

// wifi event handler (open udp port when we connect to wifi)
void WiFiEvent(WiFiEvent_t event){
    switch(event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      case IP_EVENT_STA_GOT_IP:
          Serial.print("WiFi connected! IP address: ");
          Serial.println(WiFi.localIP());
          udp.begin(udpPort);
          connected = true;
          break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      case WIFI_EVENT_STA_DISCONNECTED:
          Serial.println("WiFi lost connection");
          connected = false;
          break;
      default:
          break;
    }
}
