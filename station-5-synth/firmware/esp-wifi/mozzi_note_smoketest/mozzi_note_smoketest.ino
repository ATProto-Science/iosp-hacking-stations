/*  Station 5 — Mozzi "hello world" chime, no WiFi/relay/ATProto at all.

    Purpose: a pure hardware smoketest for the Wemos -> LM386 amp -> speaker
    chain (see ../../../../landing-page's noizetoyz-pinouts sheet for the
    amp's IN/OUT/VDD/GND pinout), isolated from synth_relay.py's downlink —
    written 2026-09-02 because the running relay's Jetstream subscription
    was stuck in a DNS-resolution-failure loop, so notes were reaching
    ATProto fine but never coming back through the downlink to actually
    make sound. This sketch doesn't touch the network at all, so it can't
    be affected by that: if the amp/speaker wiring is right, you hear a
    four-note ascending chime (C4-E4-G4-C5), pause, repeat, forever.

    For a longer/fancier melody on the same rig, see the sibling
    ../mozzi_rickroll_chime/ sketch — this one stays deliberately minimal
    so it's the fastest thing to reach for when the only question is
    "does the audio chain work at all."

    Audio out: GPIO2 / D4 on a Wemos D1 mini, fixed by Mozzi on ESP8266 (see
    esp_synth.ino's own docstring / the pinout sheet) — same pin the real
    synth sketches use, so this also confirms the physical audio wiring
    those sketches will eventually rely on.

    Includes the same startup-race fix documented in
    ../diagnostics/mozzi_startup_race_repro/ — startMozzi() briefly disrupts
    UART transmission right as it sets up its timer/interrupt, so any
    Serial.println() called immediately before it (no flush/delay) can be
    silently lost. Cheap insurance either way.
*/

#define MOZZI_CONTROL_RATE 128
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSin(SIN2048_DATA);

// C4, E4, G4, C5 — a plain major-triad-plus-octave chime, same "hello
// world" arpeggio feel as a device startup jingle.
const uint8_t CHIME_NOTES[] = {60, 64, 67, 72};
const uint8_t NUM_NOTES = sizeof(CHIME_NOTES) / sizeof(CHIME_NOTES[0]);

const unsigned int NOTE_MS = 180;   // how long each note sounds
const unsigned int GAP_MS = 60;     // silence between notes
const unsigned int REST_MS = 1800;  // pause after the full chime before repeating

int8_t noteIndex = -1;              // -1 = resting between chimes
unsigned long stepStartedAt = 0;
bool sounding = false;

float midiToFreq(uint8_t note) {
  return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[chime] Station 5 hardware smoketest — no WiFi, no relay");
  Serial.println("[chime] audio out: GPIO2 / D4");

  // THE FIX (see diagnostics/mozzi_startup_race_repro/) — without this,
  // the line above (or this one) can silently never reach the serial
  // monitor, even though the sketch itself is running fine.
  Serial.flush();
  delay(500);

  startMozzi();
  stepStartedAt = millis();
}

void startNextStep() {
  noteIndex++;
  if (noteIndex >= NUM_NOTES) {
    // finished the chime — rest, then start over from note 0
    noteIndex = -1;
    sounding = false;
    stepStartedAt = millis();
    return;
  }
  aSin.setFreq(midiToFreq(CHIME_NOTES[noteIndex]));
  sounding = true;
  stepStartedAt = millis();
}

void updateControl() {
  unsigned long elapsed = millis() - stepStartedAt;

  if (noteIndex == -1) {
    // resting between chimes (or hasn't started the first one yet)
    if (elapsed >= REST_MS || stepStartedAt == 0) {
      startNextStep();
    }
    return;
  }

  unsigned int stepLen = sounding ? NOTE_MS : GAP_MS;
  if (elapsed < stepLen) return;

  if (sounding) {
    // note just finished -> brief silence before the next one
    sounding = false;
    stepStartedAt = millis();
  } else {
    startNextStep();
  }
}

AudioOutput updateAudio() {
  if (!sounding) return MonoOutput::from8Bit(0);
  return MonoOutput::from8Bit(aSin.next());
}

void loop() {
  audioHook();
}
