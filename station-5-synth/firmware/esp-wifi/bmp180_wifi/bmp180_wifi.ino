/*  Station 2/5 crossover — BMP180 over WiFi (Wemos D1 mini + BMP180 Shield).

    Extends bmp180_smoke_test.ino (same board, same fixes — see that file's
    REAL-HARDWARE FINDING note for the Wire-hang/no-timeout issue and its
    fix, and the earlier orientation-of-the-shield lesson) with WiFi: every
    reading cycle, sends one line to
    ../../../station-2-live-data/wifi_sensor_relay.py's TCP port, which
    publishes it as two science.iosp.sensor.reading records (temperature +
    pressure — the lexicon holds one sensorType/value pair per record, not
    a bundle, so the relay splits these itself).

    Wire format (see wifi_sensor_relay.py's own docstring for the receiving
    side): temperature is pre-scaled by 10 here (matching
    sensor_producer.py's VALUE_SCALE=10 convention — AT Protocol's on-wire
    record model has no floating-point type, see that file's "VERIFIED"
    note), pressure is sent as-is (already an integer Pascal value from the
    sensor library, no scaling needed):

        temp=298 pressure=100031 deviceId=d1mini-bmp180-a

    RELAY DISCOVERY 2026-09-01: relayHost used to be a hardcoded constant
    here — and got reflashed three separate times in one session as the
    relay's IP kept changing (new WiFi network, then switching this laptop
    from ethernet to WiFi). Now fetched at boot from ATProto instead: a
    music.atproto.noizetoyz.synth.relayConfig record (../../../lexicon/, published by
    ../../../relay/publish_relay_config.py), read the same read-only way
    landing-page/viewer.html reads everything else — a plain HTTPS GET
    against HappyView's XRPC endpoint with a read-only client key, no
    OAuth, no ATProto auth logic on the MCU. Falls back to the hardcoded
    DEFAULT_RELAY_HOST if the fetch fails (network hiccup, HappyView down,
    the relayConfig lexicon not registered yet) so this never hard-depends
    on the discovery path working.

    UNTESTED past compile — about to be flashed to the real board this
    session already validated the sensor side on.
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>

#define BMP180_I2C_ADDR 0x77

Adafruit_BMP085 bmp;

// ---- fill in for your workshop WiFi ----
const char *WIFI_SSID = "noizetoyz";
const char *WIFI_PASSWORD = "synthbeep";
// ------------------------------------------------------

// Used only if the ATProto relayConfig fetch below fails.
const char *DEFAULT_RELAY_HOST = "192.168.1.20"; // laptop's wired LAN IP — WiFi stays on the home network instead
const uint16_t DEFAULT_RELAY_PORT = 8480;

// Same read-only client key already committed in landing-page/viewer.html —
// rate-limited, read-only, not a secret worth protecting.
const char *HAPPYVIEW_URL = "https://happyview.werk.museum";
const char *HAPPYVIEW_CLIENT_KEY = "hvc_4f63d844f5a253fe658f1491160126dc";

String relayHost;
uint16_t relayPort;

const char *DEVICE_ID = "d1mini-bmp180-a";
const unsigned long SEND_INTERVAL_MS = 5000; // matches sensor_producer.py's default INTERVAL_SECONDS

unsigned long lastSend = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[bmp180-wifi] connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[bmp180-wifi] WiFi up, IP=");
  Serial.println(WiFi.localIP());
}

// Fetches the most recent music.atproto.noizetoyz.synth.relayConfig record and sets
// the global relayHost/relayPort from it. Falls back to
// DEFAULT_RELAY_HOST/DEFAULT_RELAY_PORT on any failure (network error, bad
// JSON, no records yet, lexicon not registered with HappyView) — this path
// is never allowed to block startup or crash the sketch.
void fetchRelayConfig() {
  relayHost = DEFAULT_RELAY_HOST;
  relayPort = DEFAULT_RELAY_PORT;

  WiFiClientSecure httpsClient;
  httpsClient.setInsecure(); // no cert store on this MCU; same tradeoff every ESP8266 HTTPS sketch makes

  HTTPClient http;
  String url = String(HAPPYVIEW_URL) + "/xrpc/music.atproto.noizetoyz.synth.listRelayConfig?limit=10";
  Serial.print("[bmp180-wifi] fetching relay config: ");
  Serial.println(url);

  if (!http.begin(httpsClient, url)) {
    Serial.println("[bmp180-wifi] relayConfig fetch: begin() failed, using default");
    return;
  }
  http.addHeader("X-Client-Key", HAPPYVIEW_CLIENT_KEY);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[bmp180-wifi] relayConfig fetch: HTTP ");
    Serial.print(httpCode);
    Serial.println(", using default");
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  // JsonDocument (ArduinoJson 7) sizes itself dynamically — fine for a
  // handful of small config records, no fixed capacity to guess at.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[bmp180-wifi] relayConfig fetch: JSON parse failed (");
    Serial.print(err.c_str());
    Serial.println("), using default");
    return;
  }

  // HappyView doesn't guarantee list order — same reasoning as
  // landing-page/viewer.html's client-side sort by createdAt, just done
  // here with plain string comparison since ISO8601 UTC timestamps sort
  // lexicographically in chronological order too.
  JsonArray records = doc["records"].as<JsonArray>();
  String newestCreatedAt = "";
  String newestHost = "";
  uint16_t newestPort = DEFAULT_RELAY_PORT;

  for (JsonObject record : records) {
    const char *createdAt = record["createdAt"] | "";
    const char *host = record["relayHost"] | "";
    if (strlen(host) == 0) continue;
    if (String(createdAt) > newestCreatedAt) {
      newestCreatedAt = createdAt;
      newestHost = host;
      newestPort = record["sensorTcpPort"] | DEFAULT_RELAY_PORT;
    }
  }

  if (newestHost.length() > 0) {
    relayHost = newestHost;
    relayPort = newestPort;
    Serial.print("[bmp180-wifi] relay config from ATProto: ");
    Serial.print(relayHost);
    Serial.print(":");
    Serial.println(relayPort);
  } else {
    Serial.println("[bmp180-wifi] relayConfig fetch: no records found, using default");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[bmp180-wifi] starting");

  Wire.begin(D2, D1); // SDA, SCL
  Wire.setClockStretchLimit(1000); // microseconds — see bmp180_smoke_test.ino's REAL-HARDWARE FINDING note

  Wire.beginTransmission(BMP180_I2C_ADDR);
  uint8_t i2cResult = Wire.endTransmission();
  if (i2cResult != 0) {
    while (1) {
      Serial.print("[bmp180-wifi] no ACK from 0x77 (endTransmission=");
      Serial.print(i2cResult);
      Serial.println(") — check the shield is fully seated on the stacking header");
      delay(1000);
    }
  }

  if (!bmp.begin()) {
    while (1) {
      Serial.println("[bmp180-wifi] I2C ACK OK but BMP180 library begin() failed — check sensor identity/wiring");
      delay(1000);
    }
  }
  Serial.println("[bmp180-wifi] BMP180 found");

  connectWiFi();
  fetchRelayConfig();
}

void sendReading(int tempScaled, int32_t pressurePa) {
  WiFiClient client;
  if (!client.connect(relayHost.c_str(), relayPort)) {
    Serial.println("[bmp180-wifi] relay connect failed");
    return;
  }

  // One write, not several — see station-5-synth/firmware/esp-wifi/esp_synth's
  // REAL-HARDWARE FINDING note: multiple client.print() calls followed
  // immediately by client.stop() truncated messages on this exact platform.
  String line = "temp=" + String(tempScaled) + " pressure=" + String(pressurePa) + " deviceId=" + String(DEVICE_ID) + "\n";
  client.print(line);
  client.flush();
  client.stop();
  Serial.print("[bmp180-wifi] sent: ");
  Serial.print(line);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[bmp180-wifi] WiFi dropped, reconnecting...");
    connectWiFi();
  }

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();

    float tempC = bmp.readTemperature();
    int32_t pressurePa = bmp.readPressure();
    int tempScaled = (int)(tempC * 10.0);

    Serial.print("[bmp180-wifi] read: temp=");
    Serial.print(tempC);
    Serial.print("C pressure=");
    Serial.print(pressurePa);
    Serial.println("Pa");

    sendReading(tempScaled, pressurePa);
  }
}
