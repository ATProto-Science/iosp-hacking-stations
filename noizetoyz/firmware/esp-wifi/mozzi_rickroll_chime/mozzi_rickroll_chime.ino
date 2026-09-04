/*  Noizetoyz — Mozzi chime playing the "Never Gonna Give You Up" chorus
    hook, no WiFi/relay/ATProto at all. Sibling to
    ../mozzi_note_smoketest/, which stays a minimal 4-note chime — this one
    is the same hardware smoketest with a real melody, once the plain
    chime has already proven the Wemos -> LM386 amp -> speaker chain works
    (see ../../../../landing-page's noizetoyz-pinouts sheet for the amp's
    IN/OUT/VDD/GND pinout).

    Melody: the chorus hook (Rick Astley), Db major, 114 BPM — transcribed
    note-for-note from Hooktheory theorytab data. Same decoded table
    already used to build the real music.make_rickroll() track in
    ~/txt/pixelsp33d/tools/make_music.py; see
    ~/txt/pixelsp33d/RICKROLL-AUDIO-2026-08.md section 6 for the full
    derivation (raw JSON, scale-degree decode, and the legal/copyright
    research behind using the literal melody rather than an original
    pastiche — Torsten's explicit call for that project, reused here as-is
    since it's the same "melody is public musical fact, transcribe it"
    move, not new legal ground). Cross-checked programmatically against
    the doc's raw JSON (decoding every note's beat/duration/scale-degree
    directly rather than trusting the doc's prose bar-by-bar transcription
    by eye) — the doc's own transcription held up, note-for-note.

    Audio out: GPIO2 / D4 on a Wemos D1 mini, fixed by Mozzi on ESP8266 (see
    esp_synth.ino's own docstring / the pinout sheet).

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

// MIDI note numbers for the seven pitches this melody actually uses
// (Db major chorus, per the theorytab decode) — 0 means rest.
#define REST 0
#define Ab3 56
#define Bb3 58
#define C4  60
#define Db4 61
#define Eb4 63
#define F4  65
#define Ab4 68

const uint8_t BPM = 114;

// (note, duration) pairs, duration in sixteenth-note units (a quarter
// note = 4) — the chorus hook, 8 bars of 4/4 (bar 8 runs long as the
// turnaround into the loop repeat, per the source doc).
struct Step { uint8_t note; uint8_t dur16; };
const Step MELODY[] = {
  // bar 1
  {F4,3}, {F4,3}, {Eb4,4}, {REST,2}, {Ab3,1}, {Bb3,1}, {C4,1}, {Bb3,1},
  // bar 2
  {Eb4,3}, {Eb4,3}, {Db4,2}, {C4,1}, {Bb3,3},
  // bar 3
  {Ab3,1}, {Bb3,1}, {Db4,1}, {Bb3,1}, {Db4,3}, {Eb4,3}, {C4,1}, {Bb3,3},
  // bar 4
  {Ab3,2}, {REST,2}, {Ab3,2}, {Eb4,4}, {Db4,4}, {REST,4},
  // bar 5
  {Ab3,1}, {Bb3,1}, {Db4,1}, {Bb3,1}, {F4,3}, {F4,3}, {Eb4,5}, {REST,1},
  // bar 6
  {Ab3,1}, {Bb3,1}, {C4,1}, {Bb3,1}, {Ab4,3}, {C4,2}, {Db4,2}, {C4,1}, {Bb3,4},
  // bar 7
  {Ab3,1}, {Bb3,1}, {C4,1}, {Bb3,1}, {Db4,3}, {Eb4,3}, {C4,3}, {Bb3,1}, {Ab3,2},
  // bar 8 (turnaround, runs 5 beats before the loop repeats)
  {REST,2}, {Ab3,2}, {Eb4,2}, {Db4,2}, {Db4,4}, {REST,8},
};
const uint8_t NUM_STEPS = sizeof(MELODY) / sizeof(MELODY[0]);
const unsigned int REST_MS = 1200;  // pause between full loops of the hook

const float MS_PER_16TH = 60000.0 / BPM / 4.0;  // one sixteenth note, at 114 BPM
const unsigned int GAP_MS = 18;                 // brief silence before a repeated/adjacent note, so back-to-back same-pitch notes still articulate separately

int16_t stepIndex = -1;   // -1 = resting between loops
unsigned long stepStartedAt = 0;
bool sounding = false;

float midiToFreq(uint8_t note) {
  return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[chime] Noizetoyz hardware smoketest — no WiFi, no relay");
  Serial.println("[chime] audio out: GPIO2 / D4 — Never Gonna Give You Up chorus hook");

  // THE FIX (see diagnostics/mozzi_startup_race_repro/) — without this,
  // the lines above can silently never reach the serial monitor, even
  // though the sketch itself is running fine.
  Serial.flush();
  delay(500);

  startMozzi();
  stepStartedAt = millis();
}

void startNextStep() {
  stepIndex++;
  if (stepIndex >= NUM_STEPS) {
    // finished the hook — rest, then loop back to bar 1
    stepIndex = -1;
    sounding = false;
    stepStartedAt = millis();
    return;
  }
  if (MELODY[stepIndex].note == REST) {
    sounding = false;
  } else {
    aSin.setFreq(midiToFreq(MELODY[stepIndex].note));
    sounding = true;
  }
  stepStartedAt = millis();
}

void updateControl() {
  unsigned long elapsed = millis() - stepStartedAt;

  if (stepIndex == -1) {
    if (elapsed >= REST_MS || stepStartedAt == 0) {
      startNextStep();
    }
    return;
  }

  unsigned int slotMs = (unsigned int)(MELODY[stepIndex].dur16 * MS_PER_16TH);
  bool inGap = sounding && slotMs > GAP_MS && elapsed >= (slotMs - GAP_MS);
  if (inGap) {
    sounding = false;  // articulation gap before the next note, even mid-slot
  }
  if (elapsed < slotMs) return;

  startNextStep();
}

AudioOutput updateAudio() {
  if (!sounding) return MonoOutput::from8Bit(0);
  return MonoOutput::from8Bit(aSin.next());
}

void loop() {
  audioHook();
}
