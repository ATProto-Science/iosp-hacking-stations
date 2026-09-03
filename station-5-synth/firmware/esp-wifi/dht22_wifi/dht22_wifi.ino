/*  Station 2/5 crossover — DHT22 over WiFi (Wemos D1 mini), temperature +
    humidity. Same shape as ../bmp180_wifi/bmp180_wifi.ino (a second sensor
    board feeding station-2-live-data/wifi_sensor_relay.py, not another
    station-5-synth relay) — copied structurally rather than re-derived:
    relay discovery via music.atproto.noizetoyz.synth.relayConfig, the
    same wire-line format, the same "no ATProto logic on the MCU" split.

    Sensor: a 4-pin DHT22/AM2302 breakout module (VCC, DATA, NC, GND when
    facing the sensor's vent holes — confirmed against the physical module
    in hand 2026-09-03, not the bare 3-pin sensor some wiring guides show).
    4-pin modules like this one already carry the DATA-line pull-up
    resistor on the small breakout PCB — no external 10k resistor needed,
    unlike the bare-sensor wiring the DHT_sensor_library's own example
    comments describe.

    Power VCC from the Wemos's 3V3 pin, NOT 5V — the opposite gotcha from
    station-5's own LM386 amp (which specifically needs 5V for current
    headroom). DHT22 is happy anywhere in its 3.3-6V range and only draws
    ~1.5mA, so current isn't the concern here — what matters is that its
    DATA line's HIGH level tracks whatever VCC it's given, and the
    ESP8266's GPIOs are NOT 5V-tolerant (~3.6V absolute max input). Feeding
    this sensor 5V would drive the Wemos's D5 pin at ~5V too, risking that
    GPIO. 3.3V keeps DATA's HIGH level matched to the ESP8266's own logic.

    Library: Adafruit's "DHT sensor library" (+ its Adafruit_Sensor
    dependency, already installed for the BMP180 sketches) — installed
    2026-09-03 via `arduino-cli lib install "DHT sensor library"`.
    DHTPIN is D5 (GPIO14): the library's own example explicitly calls out
    GPIO3/4/5/12/13/14 as the safe pins on ESP8266 boards (D3/D4 —
    GPIO0/GPIO2 — have boot-mode strapping behavior and aren't on that
    list), and D5 doesn't collide with any convention used elsewhere in
    this station (D1/D2 are I2C on the BMP180/OLED boards, D4 is Mozzi's
    fixed audio-out pin) even though this is a separate physical board
    with no I2C or audio of its own.

    Wire format (see wifi_sensor_relay.py's own docstring for the
    receiving side, extended 2026-09-03 to also handle `humidity`): both
    values pre-scaled by 10 — temperature matching sensor_producer.py's
    existing VALUE_SCALE=10 convention, humidity scaled the same way for
    one-decimal precision (AT Protocol's on-wire record model has no
    floating-point type, see station-2-live-data's own "no floats" note):

        temp=225 humidity=451 deviceId=d1mini-dht22-a

    HARDWARE STATUS (2026-09-03): wired on a real board, not yet reading.
    Sensor was first wired to D4 (mismatch with this sketch's D5 — moved to
    D5, matching the code); after that fix it still read NaN on every
    attempt through three separate rewires (DATA/VCC/GND all reseated).
    WiFi connects fine and the relay round-trip logic is exercised (NaN
    guard fires as designed) — the fault is isolated to the DHT22 read
    itself, not the WiFi/relay path. Suspects not yet ruled out: a dead
    module, a breakout that lacks the onboard DATA pull-up its silkscreen
    implies (bare-sensor-style board mislabeled as the 4-pin kind), or a
    breadboard row/column miscount. Next session: check for a power LED on
    the module, visually confirm a pull-up resistor near DATA, and/or
    multimeter-verify 3.3V across VCC/GND at the module itself before
    rewiring again. Physical sensor work deferred a few days — this file's
    firmware logic itself is not suspected and hasn't changed since.
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

#define DHTPIN D5      // GPIO14 — see header comment for why this pin specifically
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

// ---- fill in for your workshop WiFi ----
const char *WIFI_SSID = "noizetoyz";
const char *WIFI_PASSWORD = "synthbeep";
// ------------------------------------------------------

// Used only if the ATProto relayConfig fetch below fails.
const char *DEFAULT_RELAY_HOST = "192.168.1.20";
const uint16_t DEFAULT_RELAY_PORT = 8480; // wifi_sensor_relay.py's SENSOR_TCP_PORT

// Same read-only client key already committed in landing-page/viewer.html —
// rate-limited, read-only, not a secret worth protecting.
const char *HAPPYVIEW_URL = "https://happyview.werk.museum";
const char *HAPPYVIEW_CLIENT_KEY = "hvc_4f63d844f5a253fe658f1491160126dc";

String relayHost;
uint16_t relayPort;

const char *DEVICE_ID = "d1mini-dht22-a";
const unsigned long SEND_INTERVAL_MS = 5000; // matches sensor_producer.py's default INTERVAL_SECONDS

unsigned long lastSend = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[dht22-wifi] connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[dht22-wifi] WiFi up, IP=");
  Serial.println(WiFi.localIP());
}

// Identical mechanism to bmp180_wifi.ino's own fetchRelayConfig() — see
// that file for the full reasoning. Falls back to
// DEFAULT_RELAY_HOST/DEFAULT_RELAY_PORT on any failure.
void fetchRelayConfig() {
  relayHost = DEFAULT_RELAY_HOST;
  relayPort = DEFAULT_RELAY_PORT;

  WiFiClientSecure httpsClient;
  httpsClient.setInsecure();
  // Shrink BearSSL's default ~16KB RX/TX buffers — real hardware bug fixed
  // 2026-09-03, see bmp180_wifi.ino's identical line for the full
  // diagnosis and its follow-up: 1024 bytes regressed a few hours later
  // as the (then-unbounded, ?limit=10) response kept growing. Fixed at
  // the root by bounding the query itself (?limit=5 below) instead of
  // just chasing growth with a bigger buffer; 4096 here is headroom on
  // top of that now-bounded ~1.6KB response, not a tight fit against it.
  httpsClient.setBufferSizes(4096, 512);

  HTTPClient http;
  String url = String(HAPPYVIEW_URL) + "/xrpc/music.atproto.noizetoyz.synth.listRelayConfig?limit=5";
  Serial.print("[dht22-wifi] fetching relay config: ");
  Serial.println(url);

  if (!http.begin(httpsClient, url)) {
    Serial.println("[dht22-wifi] relayConfig fetch: begin() failed, using default");
    return;
  }
  http.addHeader("X-Client-Key", HAPPYVIEW_CLIENT_KEY);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[dht22-wifi] relayConfig fetch: HTTP ");
    Serial.print(httpCode);
    Serial.println(", using default");
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[dht22-wifi] relayConfig fetch: JSON parse failed (");
    Serial.print(err.c_str());
    Serial.println("), using default");
    return;
  }

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
    Serial.print("[dht22-wifi] relay config from ATProto: ");
    Serial.print(relayHost);
    Serial.print(":");
    Serial.println(relayPort);
  } else {
    Serial.println("[dht22-wifi] relayConfig fetch: no records found, using default");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[dht22-wifi] starting");

  dht.begin();

  connectWiFi();
  fetchRelayConfig();
}

void sendReading(int tempScaled, int humidityScaled) {
  WiFiClient client;
  if (!client.connect(relayHost.c_str(), relayPort)) {
    Serial.println("[dht22-wifi] relay connect failed");
    return;
  }

  // One write, not several — see esp_synth.ino's REAL-HARDWARE FINDING
  // note: multiple client.print() calls followed immediately by
  // client.stop() truncated messages on this exact platform.
  String line = "temp=" + String(tempScaled) + " humidity=" + String(humidityScaled) + " deviceId=" + String(DEVICE_ID) + "\n";
  client.print(line);
  client.flush();
  client.stop();
  Serial.print("[dht22-wifi] sent: ");
  Serial.print(line);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[dht22-wifi] WiFi dropped, reconnecting...");
    connectWiFi();
  }

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();

    // DHT22 is slow (~2s between reads) and the library returns NaN on a
    // failed/too-frequent read — the datasheet's own minimum sample
    // interval is 2s, comfortably inside this sketch's 5s SEND_INTERVAL_MS.
    float tempC = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(tempC) || isnan(humidity)) {
      Serial.println("[dht22-wifi] read failed (NaN) — sensor not responding, will retry next interval");
      return;
    }

    int tempScaled = (int)(tempC * 10.0);
    int humidityScaled = (int)(humidity * 10.0);

    Serial.print("[dht22-wifi] read: temp=");
    Serial.print(tempC);
    Serial.print("C humidity=");
    Serial.print(humidity);
    Serial.println("%");

    sendReading(tempScaled, humidityScaled);
  }
}
