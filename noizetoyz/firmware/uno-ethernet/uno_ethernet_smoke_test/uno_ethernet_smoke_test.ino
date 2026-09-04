/*  Noizetoyz — Arduino UNO + Ethernet shield, direct-LAN smoke test.

    Third connectivity path for a UNO in this station, alongside
    ../uno-serial/uno_synth.ino (Serial -> a Pi running
    uno_serial_bridge.py) and the ESP8266/ESP32 WiFi path
    (../esp-wifi/esp_synth.ino): this one skips both WiFi and any serial
    bridge entirely — a UNO + W5100-class Ethernet shield opens its own TCP
    connection straight to the relay over wired Ethernet, plugged into a
    router's ordinary LAN port. Simpler than the serial-bridge path (no
    framing, no separate bridge process) once you have a shield and a spare
    LAN port; started 2026-09-01 after finding the TP-Link TL-WR841N v9
    earmarked for the (USB) bridge idea has no USB port on the hardware at
    all — this sidesteps that entirely, using the same router's plain LAN
    ports instead.

    No buttons wired — same "prove the network path first, with zero
    peripherals" approach as every other *_smoke_test.ino in this station.
    Sends one wire-format line every 3s in the exact format
    synth_relay.py's parse_line() expects:

        note=60 velocity=100 deviceId=uno-ethernet-a synthType=smoketest

    Circuit: standard Arduino Ethernet shield (WIZnet W5100/W5200/W5500),
    stacked on the UNO, SPI pins + CS on pin 10 (Ethernet library default —
    see Ethernet.init() below if your shield uses a different CS pin).
    Ethernet cable from the shield's RJ45 jack to a router LAN port.

    UNTESTED — no shield in hand to verify against while writing this; the
    single-write-before-stop() pattern below is applied preventively, not
    because it's been confirmed as a bug on EthernetClient specifically
    (only confirmed on ESP8266's WiFiClient so far, see
    ../esp-wifi/smoke_test/smoke_test.ino's REAL-HARDWARE FINDING note) —
    cheap enough to do defensively either way.
*/

#include <SPI.h>
#include <Ethernet.h>

// Newer Ethernet shields print a MAC address on a sticker — use that if
// yours has one; this placeholder works fine too as long as nothing else
// on the same LAN segment uses it.
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

const char *RELAY_HOST = "192.168.1.15"; // whatever machine runs synth_relay.py
const uint16_t RELAY_PORT = 8477;        // synth_relay.py's SYNTH_TCP_PORT
const char *DEVICE_ID = "uno-ethernet-a";

EthernetClient client;
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 3000;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[uno-ethernet] starting, requesting DHCP lease...");

  // Ethernet.init(10); // default CS pin for most shields — uncomment/adjust if yours differs

  if (Ethernet.begin(mac) == 0) {
    Serial.println("[uno-ethernet] DHCP failed");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("[uno-ethernet] no Ethernet hardware detected — check the shield is seated");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("[uno-ethernet] link is down — check the cable");
    }
    while (true) {
      delay(1000);
    }
  }

  Serial.print("[uno-ethernet] got IP: ");
  Serial.println(Ethernet.localIP());
}

void sendNoteEvent() {
  Serial.print("[uno-ethernet] connecting to relay ");
  Serial.print(RELAY_HOST);
  Serial.print(":");
  Serial.print(RELAY_PORT);
  Serial.print(" ... ");

  if (!client.connect(RELAY_HOST, RELAY_PORT)) {
    Serial.println("FAILED");
    return;
  }
  Serial.println("connected");

  // One write, not several, before stop() — same defensive pattern as the
  // ESP8266 sketches (see this file's header comment for why it's
  // preventive here, not confirmed-necessary).
  String line = "note=60 velocity=100 deviceId=" + String(DEVICE_ID) + " synthType=smoketest\n";
  client.print(line);
  client.flush();
  client.stop();
  Serial.print("[uno-ethernet] sent: ");
  Serial.print(line);
}

void loop() {
  Ethernet.maintain(); // keep the DHCP lease renewed

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    sendNoteEvent();
  }
}
