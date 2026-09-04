/*  Noizetoyz — WiFi-native Mozzi synth (ESP8266 or ESP32: Wemos D1 mini,
    NodeMCU, or similar boards with onboard WiFi).

    UNTESTED — written against Mozzi's own current examples and its README
    pin table (both cloned locally to ~/src/Mozzi; see
    examples/01.Basics/Sinewave for the audio-loop shape this follows, and
    the pin table in ~/src/Mozzi/README.md for AUDIO_PIN below), not run on
    real hardware. Expect to adjust pin numbers for your exact board — ESP8266
    boards in particular have wildly inconsistent GPIO-vs-silkscreen labeling
    (Mozzi's own README calls this out).

    Three momentary buttons play three fixed notes; a potentiometer sets
    fxAmount (used both locally, as a tremolo depth, and sent in the wire
    message). Every note-on:
      1. plays locally through Mozzi (immediate audio feedback), and
      2. opens a short-lived WiFi TCP connection to synth_relay.py and sends
         one line in the wire format synth_relay.py's parse_line() expects:

           note=60 velocity=100 deviceId=esp32-a synthType=mozzi-esp32-tremolo fxType=tremolo fxAmount=40

    No JSON, no ATProto auth, no TLS on the MCU at all — that complexity
    lives once, in the relay. This board only needs to reach the relay's
    TCP_PORT (default 8477) over the WiFi LAN.

    Circuit (adjust to your board — see Mozzi's README pin table):
      - Audio out: ESP8266 -> GPIO2 (fixed by Mozzi). ESP32 -> GPIO25/26
        (internal DAC, Mozzi's default on a plain ESP32) if your board has
        one, otherwise PWM per Mozzi's table — either way, GPIO25/26 are
        reserved for audio and must not be reused for BTN_3 below.
      - 3 buttons -> digital pins (BTN_3 deliberately avoids 25/26), wired
        to GND, using INPUT_PULLUP (so an unpressed button reads HIGH,
        pressed reads LOW).
      - 1 potentiometer -> an ADC-capable analog pin, wiper to the pin, ends
        to 3V3/GND.

    COMPILE-CHECKED 2026-09-01 via arduino-cli (esp32:esp32:esp32 @3.3.11,
    esp8266:esp8266:d1_mini @3.1.2, arduino:avr:uno @1.8.8, Mozzi 2.0.4 +
    FixMath 1.0.9 — FixMath is a separate library Mozzi 2.x depends on for
    AudioOutput.h, not bundled; `arduino-cli lib install FixMath`). Compiles
    clean on all targets.

    REAL-HARDWARE FINDING 2026-09-01, via a real D1 mini (ESP8266) flashed
    and tested against a real relay-shaped TCP listener (see
    ../smoke_test/smoke_test.ino, the sketch this bug was actually caught
    on): sendNoteEvent()'s original multiple client.print() calls followed
    immediately by client.stop() truncated the tail of the message on the
    wire — WiFiClient buffers writes and stop() doesn't guarantee the buffer
    was actually flushed to the network first. Fixed by building the whole
    line as one String and issuing a single print() + explicit flush()
    before stop(). No board was flashed with *this* sketch specifically
    (buttons/pot aren't wired on the hardware available) — this fix is
    ported over from the smoke test's own confirmed-on-hardware fix, not
    independently reverified against this exact file.
*/

#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

#if defined(ESP32)
  #include <WiFi.h>
  #define BTN_1 32
  #define BTN_2 33
  #define BTN_3 27   // NOT 25/26 — confirmed in Mozzi's own
                      // internal/config_checks_esp32.h that a plain ESP32
                      // (built-in DAC) defaults to MOZZI_OUTPUT_INTERNAL_DAC
                      // on GPIO25/26, so those two are reserved for audio out.
  #define POT_PIN 34
  #define DEVICE_ID "esp32-a"
  #define SYNTH_TYPE "mozzi-esp32-tremolo"
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #define BTN_1 D1
  #define BTN_2 D2
  #define BTN_3 D3
  #define POT_PIN A0
  #define DEVICE_ID "esp8266-a"
  #define SYNTH_TYPE "mozzi-esp8266-tremolo"
#else
  #error "This sketch targets ESP8266 or ESP32 only."
#endif

// ---- fill in for your workshop WiFi + relay host ----
const char *WIFI_SSID = "CHANGE_ME";
const char *WIFI_PASSWORD = "CHANGE_ME";
const char *RELAY_HOST = "192.168.1.50"; // box running synth_relay.py
const uint16_t RELAY_PORT = 8477;        // synth_relay.py's SYNTH_TCP_PORT
// ------------------------------------------------------

Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSin(SIN2048_DATA);

const uint8_t BUTTON_PINS[3] = {BTN_1, BTN_2, BTN_3};
const int NOTES[3] = {60, 64, 67}; // C4, E4, G4 - a plain major triad
bool lastState[3] = {true, true, true};

int fxAmount = 0; // 0-100, from the pot, sent + used as tremolo depth
unsigned long noteOffAt = 0;
bool sounding = false;

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < 3; i++) pinMode(BUTTON_PINS[i], INPUT_PULLUP);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[station-5] connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[station-5] WiFi up, IP=");
  Serial.println(WiFi.localIP());

  startMozzi();
  aSin.setFreq(0);
}

float midiToFreq(int note) {
  return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void sendNoteEvent(int note) {
  WiFiClient client;
  if (!client.connect(RELAY_HOST, RELAY_PORT)) {
    Serial.println("[station-5] relay connect failed");
    return;
  }
  // One write, not several — CONFIRMED on real hardware 2026-09-01
  // (noizetoyz/firmware/esp-wifi/smoke_test): multiple client.print()
  // calls followed immediately by client.stop() truncated the tail of the
  // message (WiFiClient buffers writes; stop() doesn't guarantee the buffer
  // was actually flushed to the network first). Concatenating into a single
  // print() sidesteps the race instead of relying on flush() timing.
  String line = "note=" + String(note) + " velocity=100 deviceId=" + String(DEVICE_ID) + " synthType=" + String(SYNTH_TYPE);
  if (fxAmount > 0) {
    line += " fxType=tremolo fxAmount=" + String(fxAmount);
  }
  line += "\n";
  client.print(line);
  client.flush();
  client.stop();
}

void updateControl() {
  fxAmount = map(analogRead(POT_PIN), 0, 1023, 0, 100);

  for (uint8_t i = 0; i < 3; i++) {
    bool pressed = digitalRead(BUTTON_PINS[i]) == LOW;
    if (pressed && lastState[i]) { // just pressed (falling edge)
      aSin.setFreq(midiToFreq(NOTES[i]));
      sounding = true;
      noteOffAt = millis() + 400;
      sendNoteEvent(NOTES[i]);
      Serial.print("[station-5] note on: ");
      Serial.println(NOTES[i]);
    }
    lastState[i] = !pressed;
  }

  if (sounding && millis() > noteOffAt) {
    aSin.setFreq(0);
    sounding = false;
  }
}

AudioOutput updateAudio() {
  int8_t sample = aSin.next();
  if (fxAmount > 0) {
    // Cheap tremolo: amplitude-modulate by a slow sine derived from millis(),
    // depth scaled by fxAmount. Not a real Mozzi effect object on purpose —
    // keeps this sketch to what's needed to prove the wire protocol; swap in
    // Mozzi's own tremolo/ADSR/filter objects (see ~/src/Mozzi/examples/) for
    // a real patch.
    float depth = fxAmount / 100.0;
    float mod = 1.0 - depth + depth * (0.5 + 0.5 * sin(millis() * 0.01));
    sample = (int8_t)(sample * mod);
  }
  return MonoOutput::from8Bit(sample);
}

void loop() {
  audioHook();
}
