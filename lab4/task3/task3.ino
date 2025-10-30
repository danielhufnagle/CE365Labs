#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ----- WIFI SETTINGS + API STUFF ----- //
const char* WIFI_SSID = "phone";         // set your SSID
const char* WIFI_PASSWORD = "googlehome"; // set your password
const char* WEATHERAPI_KEY = "a0465835981045d68db150408252810"; // get one at weatherapi.com

// ----- OLED config ----- //
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- Helpers ----- //

// put up to 3 lines ot text on the display
void showText(const String& line1, const String& line2 = "", const String& line3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2.length()) display.println(line2);
  if (line3.length()) display.println(line3);
  display.display();
}


// connect to wifi
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  showText("Connecting WiFi...", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    showText("WiFi connected", WiFi.localIP().toString());
    return true;
  }
  showText("WiFi failed", "Check SSID/pass");
  return false;
}


// get the json from the weather api
bool httpGetJson(const String& url, DynamicJsonDocument& doc) {
  HTTPClient http;
  http.setTimeout(8000);
  WiFiClientSecure* secureClient = nullptr;
  bool begun = false;
  // Parse scheme/host/path so we can call begin with explicit host/port/path for HTTPS
  if (url.startsWith("https://") || url.startsWith("http://")) {
    bool isHttps = url.startsWith("https://");
    String rest = url;
    rest.remove(0, isHttps ? 8 : 7); // strip scheme
    int slash = rest.indexOf('/');
    String host = (slash == -1) ? rest : rest.substring(0, slash);
    String path = (slash == -1) ? String("/") : rest.substring(slash);
    uint16_t port = isHttps ? 443 : 80;
    Serial.println("HTTP connecting to host: " + host + " port: " + String(port) + " path: " + path);
    if (isHttps) {
      secureClient = new WiFiClientSecure();
      secureClient->setInsecure(); // skip cert verification
      begun = http.begin(*secureClient, host.c_str(), port, path.c_str());
      if (!begun) {
        Serial.println("HTTP begin() (secure) failed for host: " + host);
        delete secureClient;
        return false;
      }
    } else {
      begun = http.begin(url);
      if (!begun) {
        Serial.println("HTTP begin() failed for: " + url);
        return false;
      }
    }
  } else {
    // Unknown scheme: try plain begin
    if (!http.begin(url)) {
      Serial.println("HTTP begin() failed for: " + url);
      return false;
    }
  }

  int code = http.GET();
  String payload = http.getString();
  Serial.printf("HTTP GET returned code=%d payload_len=%d\n", code, (int)payload.length());
  if (payload.length()) {
    Serial.println("Response payload:");
    Serial.println(payload);
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    if (secureClient) delete secureClient;
    return false;
  }

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
  }
  http.end();
  if (secureClient) delete secureClient;
  return !err;
}

// get the latitude and longitude information from api
bool fetchLatLon(float& latOut, float& lonOut) {
  DynamicJsonDocument doc(1024);
  if (!httpGetJson("http://ip-api.com/json", doc)) return false;
  if (!doc.containsKey("lat") || !doc.containsKey("lon")) return false;
  latOut = doc["lat"].as<float>();
  lonOut = doc["lon"].as<float>();
  return true;
}


// get weather information from api
bool fetchWeather(float lat, float lon, String& temperatureC, String& description) {
  String url = String("https://api.weatherapi.com/v1/current.json?key=") + WEATHERAPI_KEY + "&q=" + String(lat, 6) + "," + String(lon, 6);
  // String url = String("https://api.weatherapi.com/v1/current.json?key=") + WEATHERAPI_KEY + "&q=" + "51.52" + "," + "-0.11"; // test api is working with London
  DynamicJsonDocument doc(4096);
  if (!httpGetJson(url, doc)) return false;
  JsonVariant current = doc["current"];
  if (current.isNull()) return false;
  float tempC = current["temp_c"].as<float>();
  const char* text = current["condition"]["text"].as<const char*>();
  temperatureC = String(tempC, 1) + " C";
  description = text ? String(text) : String("(n/a)");
  return true;
}

void setup() {
  Serial.begin(115200);
  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    // If display fails, continue but no UI
  } else {
    display.clearDisplay();
    display.display();
  }

  showText("Booting...\nESP32-S3 Weather");

  if (!connectWiFi()) {
    return;
  }

  float lat = 0, lon = 0;
  showText("Getting location...", "ip-api.com");
  if (!fetchLatLon(lat, lon)) {
    showText("Location failed", "ip-api.com error");
    return;
  }

  showText("Fetching weather...", String(lat, 3) + "," + String(lon, 3));
  String tempC, desc;
  if (!fetchWeather(lat, lon, tempC, desc)) {
    showText("Weather failed", "Check API key");
    return;
  }

  showText("Weather now:", tempC, desc);
}

void loop() {
  // Nothing periodic; keep display on
  delay(1000);
}


