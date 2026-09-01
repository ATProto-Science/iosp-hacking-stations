/*  Station 5 — Arduino UNO Mozzi synth, serial-out variant.

    UNTESTED — same basis as ../esp-wifi/esp_synth.ino: written against
    Mozzi's own current examples (cloned to ~/src/Mozzi), not run on real
    hardware.

    An UNO has no network of its own, so the split here is:
      - This sketch: plays audio locally via Mozzi, and on every note-on
        prints one line to Serial in the exact wire format
        synth_relay.py's parse_line() expects (same format the ESP variant
        sends over WiFi TCP):

          note=60 velocity=100 deviceId=uno-bridge-1 synthType=mozzi-uno-am fxType=tremolo fxAmount=40

      - A separate box (an OpenWrt router with this UNO plugged into its USB)
        forwards those serial bytes onto the network as a raw TCP stream, to
        synth_relay.py's TCP_PORT (default 8477) — see ../../README.md for
        the exact socat/ser2net command. No custom bridge code needed: a
        line-oriented serial stream forwarded byte-for-byte over TCP is
        already exactly what synth_relay.py's LineHandler expects, so a
        stock serial<->TCP tool is enough.

    Circuit (per Mozzi's README pin table): audio out on digital pin 9.
    3 buttons on digital pins 2-4, wired to GND, using INPUT_PULLUP. 1
    potentiometer on A0 for fxAmount, wiper to A0, ends to 5V/GND.

    COMPILE-CHECKED 2026-09-01 via arduino-cli (arduino:avr:uno @1.8.8,
    Mozzi 2.0.4 + FixMath 1.0.9 — FixMath is a separate library Mozzi 2.x
    depends on for AudioOutput.h, not bundled; `arduino-cli lib install
    FixMath`). 8166 bytes flash (25%), 795 bytes RAM (38%) — comfortably
    under the UNO's 32256/2048 limits. Compiles clean. This only proves the
    code builds — no board was flashed, no audio/serial/bridge connectivity
    has been verified.
*/

#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

#define BTN_1 2
#define BTN_2 3
#define BTN_3 4
#define POT_PIN A0
#define DEVICE_ID "uno-bridge-1"
#define SYNTH_TYPE "mozzi-uno-am"

Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSin(SIN2048_DATA);

const uint8_t BUTTON_PINS[3] = {BTN_1, BTN_2, BTN_3};
const int NOTES[3] = {60, 64, 67}; // C4, E4, G4 - a plain major triad
bool lastState[3] = {true, true, true};

int fxAmount = 0; // 0-100, from the pot
unsigned long noteOffAt = 0;
bool sounding = false;

void setup() {
  Serial.begin(115200); // match the baud rate in the OpenWrt bridge command
  for (uint8_t i = 0; i < 3; i++) pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  startMozzi();
  aSin.setFreq(0);
}

float midiToFreq(int note) {
  return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void sendNoteEvent(int note) {
  Serial.print(F("note="));
  Serial.print(note);
  Serial.print(F(" velocity=100 deviceId="));
  Serial.print(DEVICE_ID);
  Serial.print(F(" synthType="));
  Serial.print(SYNTH_TYPE);
  if (fxAmount > 0) {
    Serial.print(F(" fxType=tremolo fxAmount="));
    Serial.print(fxAmount);
  }
  Serial.print('\n');
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
    // Same cheap amplitude-modulation trick as the ESP variant — see that
    // sketch's updateAudio() comment. Swap in a real Mozzi effect object
    // (~/src/Mozzi/examples/) for a proper patch.
    float depth = fxAmount / 100.0;
    float mod = 1.0 - depth + depth * (0.5 + 0.5 * sin(millis() * 0.01));
    sample = (int8_t)(sample * mod);
  }
  return MonoOutput::from8Bit(sample);
}

void loop() {
  audioHook();
}
