/*  Noizetoyz — WiFi+relay smoke test (ESP8266).

    No Mozzi, no audio, no buttons — just proves the network path end to
    end on real hardware: join WiFi, open a TCP connection to the relay's
    line port, send one fake note-on every 3s in the exact wire format
    synth_relay.py's parse_line() (or a throwaway listener standing in for
    it) expects, print status to Serial the whole way.

    Hardware: Wemos D1 mini + OLED Shield stack (the OLED itself isn't used
    here — this only needs the D1 mini's onboard WiFi + USB-serial).

    2026-09-01: flashed to a real D1 mini, joining "freshtomato" — the AP
    side (wl0.1) of an old Linksys WRT54G running FreshTomato 2020.2, the
    same router earmarked for the workshop's Arduino UNO tty<>net bridge.
*/

#include <ESP8266WiFi.h>

const char *WIFI_SSID = "freshtomato";
const char *WIFI_PASSWORD = "freshtomato";
const char *RELAY_HOST = "192.168.1.10"; // laptop running synth_relay.py (or the throwaway test listener)
const uint16_t RELAY_PORT = 8477;        // synth_relay.py's SYNTH_TCP_PORT

const char *DEVICE_ID = "d1mini-oled-smoketest";

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 3000;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[smoketest] connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[smoketest] WiFi up, IP=");
  Serial.println(WiFi.localIP());
}

void sendNoteEvent() {
  WiFiClient client;
  Serial.print("[smoketest] connecting to relay ");
  Serial.print(RELAY_HOST);
  Serial.print(":");
  Serial.print(RELAY_PORT);
  Serial.print(" ... ");
  if (!client.connect(RELAY_HOST, RELAY_PORT)) {
    Serial.println("FAILED");
    return;
  }
  Serial.println("connected");

  // One write, not several — multiple client.print() calls followed
  // immediately by client.stop() truncated the tail of the message on real
  // hardware (WiFiClient buffers writes; stop() doesn't guarantee the
  // buffer was actually flushed to the network first). Concatenating into
  // a single print() sidesteps the race instead of relying on flush()
  // timing.
  String line = "note=60 velocity=100 deviceId=" + String(DEVICE_ID) + " synthType=smoketest\n";
  client.print(line);
  client.flush();
  client.stop();
  Serial.print("[smoketest] sent: ");
  Serial.print(line);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[smoketest] synth WiFi+relay smoke test starting");
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[smoketest] WiFi dropped, reconnecting...");
    connectWiFi();
  }

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    sendNoteEvent();
  }
}
