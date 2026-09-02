/*  Station 5 — esp_multi_synth.ino's full mode dispatch (tone+fx, scrub,
    fold, filter, fm, pluck, plus the hidden rickroll easter egg), PLUS
    esp_note_player.ino's OLED shield (Wemos D1 mini + SSD1306, three-band
    64x48 layout). Not an edit to either — a new sibling combining the two,
    same pattern as every other hardware-combination sketch in this station
    (mozzi_note_smoketest.ino / mozzi_rickroll_chime.ino were deliberately
    kept separate rather than merged; this sketch is the same move, one
    level up).

    Reused near-verbatim from esp_multi_synth.ino: WiFi connect, relay
    discovery (fetchRelayConfig), downlink connect/reconnect
    (connectDownlink/pollDownlink), the manual key=value wire-line parser
    (applyLine) and its dispatch to all six sound modes, and the rickroll
    easter egg's MELODY[] playback. See that file's own header for the
    provenance of each mode's Mozzi API usage (WaveFolder, Sample+Line,
    ResonantFilter, phMod FM, Ead pluck envelope, AudioDelayFeedback/
    ReverbTank fx) — none of that is re-derived here, only copied.

    Reused near-verbatim from esp_note_player.ino: OLED I2C setup on
    D2/D1 with the presence-probe ACK check at 0x3C before display.begin()
    (halts with a repeating serial error if the shield isn't seated), the
    three-band screen layout (info line / decaying-envelope waveform /
    scrolling footer marquee), and the ~20fps redraw throttle inside
    updateControl() (Mozzi owns loop() via audioHook(); a full I2C redraw
    every control tick would stall audio timing).

    New in this file (the actual reason it exists — esp_note_player.ino
    only ever played plain tone, esp_multi_synth.ino has no display):
      - buildInfoLine(): extends the old "note name + duration" info line
        into a per-mode summary showing which mode is active and its key
        parameter(s), short enough to fit a 64px-wide text-size-1 line
        (roughly 10-11 characters — esp_note_player.ino's own "C4 400ms"
        line was 8). One line per mode:
          tone   "C4 tremolo" / "C4 delay" / "C4 reverb" / "C4" (no fx)
          fold   "FOLD 90/30"      (gain/bias, 0-127 each)
          filter "FILT800/64"      (cutoffHz/resonance)
          fm     "FM2.1/1.8"       (fmIndexNorm/fmRatioNorm, 1 decimal)
          pluck  "PLUCK C4"        (note name)
          scrub  "SCRB b0:64"      (sample abbrev b0/b1/rv + scrubPos)
        Filter/fm/pluck/scrub weren't shown at all in esp_note_player.ino
        (it predates every mode but tone) — these formats are new.
      - The waveform's frequency/amplitude mapping (noteToVisualFreq() +
        the exponential decay envelope) now drives off *any* mode with a
        note (tone/fold/filter/fm/pluck) via the shared lastNote/
        noteStartAt state, updated by applyLine() on every one of those
        modes' note-ons — not just tone. Scrub has no single "note", so it
        gets its own scrubToVisualFreq(scrubPos) mapping instead, and still
        re-triggers the same decay envelope on every scrub update (so the
        wave still visibly "pulses" as scrub messages arrive, instead of
        going flat).
      - The rickroll easter egg gets a distinct visual beat instead of
        reading as an ordinary note: the info line inverts (white-on-black
        via a filled rect) and flashes "RICKROLL!" at ~2Hz, and the footer
        marquee swaps to "NEVER GONNA GIVE YOU UP" for the egg's duration,
        both restored to normal the moment playback returns to MODE_TONE.

    Audio out: fixed GPIO2/D4 on ESP8266 (Mozzi, fixed pin). OLED on D1/D2
    I2C (SCL/SDA) — no pin conflict, already confirmed in esp_note_player.ino.
    Same startup-race fix as every Mozzi+ESP8266 sketch in this station:
    Serial.flush(); delay(500); immediately before startMozzi().

    UNTESTED as this combination on real hardware — assembled from two
    independently-verified sketches (esp_multi_synth.ino compiles clean at
    41% flash / 61% RAM; esp_note_player.ino's OLED addition was verified
    working on the Wemos+shield stack), not independently confirmed
    together. Watch RAM headroom in particular: esp_multi_synth.ino alone
    was already at 61%, and the SSD1306 framebuffer (64*48/8 = 384 bytes)
    plus Adafruit_GFX/Adafruit_SSD1306's own overhead adds on top of that —
    see this sketch's compile output for the real number.
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define MOZZI_CONTROL_RATE 128
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <WaveFolder.h>
#include <Sample.h>
#include <Line.h>
#include <ResonantFilter.h>
#include <Ead.h>
#include <AudioDelayFeedback.h>
#include <ReverbTank.h>
#include <samples/bamboo/bamboo_00_2048_int8.h>
#include <samples/bamboo/bamboo_01_2048_int8.h>
#include <samples/raven_arh_int8.h>

#define SSD1306_64_48
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- fill in for your workshop WiFi ----
const char *WIFI_SSID = "noizetoyz";
const char *WIFI_PASSWORD = "synthbeep";
// ------------------------------------------------------

// Relay location discovered via ATProto at boot — see esp_note_player.ino's
// identical fetchRelayConfig() for the full reasoning. Only the fallback
// if that fetch fails.
const char *DEFAULT_RELAY_HOST = "192.168.1.20";
const uint16_t DEFAULT_RELAY_PORT = 8479; // synth_relay.py's SYNTH_BROADCAST_PORT

const char *HAPPYVIEW_URL = "https://happyview.werk.museum";
const char *HAPPYVIEW_CLIENT_KEY = "hvc_4f63d844f5a253fe658f1491160126dc";

String relayHost;
uint16_t relayPort;
WiFiClient downlink;
String lineBuffer;

// ---- OLED ----
#define OLED_RESET 0
Adafruit_SSD1306 display(OLED_RESET);

// ---- tone + fold carrier ----
Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSin(SIN2048_DATA);

// ---- fold ----
WaveFolder<> wf;
uint8_t foldGain = 64;
uint8_t foldBias = 64;

// ---- filter ----
Oscil<SAW2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSaw(SAW2048_DATA);
LowPassFilter rf; // ResonantFilter<uint8_t, LOWPASS>
int lastCutoffHz = 800;
int lastResonance = 64;

// ---- fm ----
Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aFmCarrier(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aFmModulator(SIN2048_DATA);
float fmIndexNorm = 1.0; // 0..~4 cycles of phase deviation
float fmRatioNorm = 1.0; // 0.5..4.0x carrier freq — display-only mirror of applyLine()'s local calc

// ---- pluck ----
Ead pluckEnvelope(MOZZI_CONTROL_RATE);

// ---- tone fx: tremolo (hand-rolled, esp_synth.ino's trick), delay, reverb ----
enum FxType { FX_NONE, FX_TREMOLO, FX_DELAY, FX_REVERB };
FxType currentFx = FX_NONE;
AudioDelayFeedback<128> aDelay;
ReverbTank reverb;

// ---- scrub ----
Sample<BAMBOO_00_2048_NUM_CELLS, MOZZI_AUDIO_RATE, INTERP_LINEAR> aSampleBamboo00(BAMBOO_00_2048_DATA);
Sample<BAMBOO_01_2048_NUM_CELLS, MOZZI_AUDIO_RATE, INTERP_LINEAR> aSampleBamboo01(BAMBOO_01_2048_DATA);
Sample<RAVEN_ARH_NUM_CELLS, MOZZI_AUDIO_RATE, INTERP_LINEAR> aSampleRaven(RAVEN_ARH_DATA);
Line<Q16n16> scrub;
const unsigned int AUDIO_STEPS_PER_CONTROL = MOZZI_AUDIO_RATE / MOZZI_CONTROL_RATE;

enum SampleChoice { SAMPLE_BAMBOO00, SAMPLE_BAMBOO01, SAMPLE_RAVEN };
SampleChoice currentSample = SAMPLE_BAMBOO00;
int lastScrubPos = 64;

long numCellsForSample(SampleChoice s) {
  switch (s) {
    case SAMPLE_BAMBOO00: return BAMBOO_00_2048_NUM_CELLS;
    case SAMPLE_BAMBOO01: return BAMBOO_01_2048_NUM_CELLS;
    case SAMPLE_RAVEN:    return RAVEN_ARH_NUM_CELLS;
  }
  return BAMBOO_00_2048_NUM_CELLS;
}

const char *sampleAbbrev(SampleChoice s) {
  switch (s) {
    case SAMPLE_BAMBOO00: return "b0";
    case SAMPLE_BAMBOO01: return "b1";
    case SAMPLE_RAVEN:    return "rv";
  }
  return "b0";
}

// ---- rickroll easter egg — MELODY[] copied verbatim from
// mozzi_rickroll_chime.ino via esp_multi_synth.ino; see either for the
// transcription's provenance/verification. ----
#define REST 0
#define Ab3 56
#define Bb3 58
#define C4  60
#define Db4 61
#define Eb4 63
#define F4  65
#define Ab4 68
const uint8_t RICKROLL_BPM = 114;
struct Step { uint8_t note; uint8_t dur16; };
const Step MELODY[] = {
  {F4,3}, {F4,3}, {Eb4,4}, {REST,2}, {Ab3,1}, {Bb3,1}, {C4,1}, {Bb3,1},
  {Eb4,3}, {Eb4,3}, {Db4,2}, {C4,1}, {Bb3,3},
  {Ab3,1}, {Bb3,1}, {Db4,1}, {Bb3,1}, {Db4,3}, {Eb4,3}, {C4,1}, {Bb3,3},
  {Ab3,2}, {REST,2}, {Ab3,2}, {Eb4,4}, {Db4,4}, {REST,4},
  {Ab3,1}, {Bb3,1}, {Db4,1}, {Bb3,1}, {F4,3}, {F4,3}, {Eb4,5}, {REST,1},
  {Ab3,1}, {Bb3,1}, {C4,1}, {Bb3,1}, {Ab4,3}, {C4,2}, {Db4,2}, {C4,1}, {Bb3,4},
  {Ab3,1}, {Bb3,1}, {C4,1}, {Bb3,1}, {Db4,3}, {Eb4,3}, {C4,3}, {Bb3,1}, {Ab3,2},
  {REST,2}, {Ab3,2}, {Eb4,2}, {Db4,2}, {Db4,4}, {REST,8},
};
const uint8_t NUM_STEPS = sizeof(MELODY) / sizeof(MELODY[0]);
const float RICKROLL_MS_PER_16TH = 60000.0 / RICKROLL_BPM / 4.0;
const unsigned int RICKROLL_GAP_MS = 18;
int16_t rickrollStep = -1;
unsigned long rickrollStepAt = 0;

// ---- shared playback state ----
enum PlayMode { MODE_TONE, MODE_SCRUB, MODE_FOLD, MODE_FILTER, MODE_FM, MODE_PLUCK, MODE_RICKROLL };
PlayMode currentMode = MODE_TONE;

const unsigned int NOTE_DURATION_MS = 400;
bool sounding = false;
unsigned long noteOffAt = 0;
uint8_t fxAmount = 0; // tone mode's tremolo depth, 0-100 (existing field's own range)

// ---- OLED display state (esp_note_player.ino's layout, extended) ----
// Screen layout — 64x48 total, split into three bands:
//   y  0- 7: per-mode info line (text size 1, 8px tall) — see buildInfoLine()
//   y 10-37: waveform, in its own 28px band
//   y 40-47: scrolling footer marquee (text size 1, 8px tall)
const int W = 64;
const int WAVE_TOP = 10;
const int WAVE_HEIGHT = 28;
const int WAVE_MID = WAVE_TOP + WAVE_HEIGHT / 2;

int lastNote = 60; // last note-on across tone/fold/filter/fm/pluck — drives the waveform when not in scrub

float phase = 0.0;
const float PHASE_STEP = 0.25;
// Amplitude decays from PEAK down to IDLE after each note/scrub update,
// exponentially, instead of staying constant — same "settling" feel as
// esp_note_player.ino, now re-triggered by any mode's update (see
// noteStartAt below), not just tone.
const float PEAK_AMPLITUDE = 14.0;
const float IDLE_AMPLITUDE = 2.0;
const float DECAY_TAU_MS = 600.0; // time constant — bigger = slower decay
unsigned long noteStartAt = 0;
unsigned long lastDraw = 0;
const unsigned long DRAW_INTERVAL_MS = 50; // ~20fps — see header comment on why this is throttled

const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

const char *FOOTER_TEXT = "ATScience|ATMusic|IOSP Hacking Station";
const char *RICKROLL_FOOTER_TEXT = "NEVER GONNA GIVE YOU UP - NEVER GONNA LET YOU DOWN";
float footerX = 64; // starts off the right edge, scrolls left
const float FOOTER_STEP = 0.6; // slower than the waveform's own phase — text needs to stay readable

float midiToFreq(int note) {
  return 440.0 * pow(2.0, (note - 69) / 12.0);
}

// Maps a MIDI note to the waveform's visual cycles-per-pixel — higher notes
// draw a denser/faster-looking wave, lower notes a wider one. Verbatim from
// esp_note_player.ino.
float noteToVisualFreq(int note) {
  return 0.05 + (constrain(note, 36, 84) - 36) / 48.0 * 0.35;
}

// Scrub's fallback visual mapping — scrub has no single "note" the way the
// other modes do, so its scrubPos (0-127, same range note-ish values get
// constrained to above) drives the same visual-frequency curve instead.
float scrubToVisualFreq(int scrubPos) {
  return 0.05 + constrain(scrubPos, 0, 127) / 127.0 * 0.35;
}

// "C4" style name — MIDI note 60 is C4 by the standard convention this
// follows (octave = note/12 - 1).
String midiNoteName(int note) {
  int octave = note / 12 - 1;
  return String(NOTE_NAMES[note % 12]) + String(octave);
}

// Per-mode info line — see this file's header comment for the format
// chosen per mode and why. Rickroll is handled separately in
// drawInfoLine() (it gets a flashing/inverted treatment, not this).
String buildInfoLine() {
  switch (currentMode) {
    case MODE_FOLD:
      return "FOLD " + String(foldGain) + "/" + String(foldBias);
    case MODE_FILTER:
      return "FILT" + String(lastCutoffHz) + "/" + String(lastResonance);
    case MODE_FM:
      return "FM" + String(fmIndexNorm, 1) + "/" + String(fmRatioNorm, 1);
    case MODE_PLUCK:
      return "PLUCK " + midiNoteName(lastNote);
    case MODE_SCRUB:
      return "SCRB " + String(sampleAbbrev(currentSample)) + ":" + String(lastScrubPos);
    default: { // MODE_TONE (and MODE_RICKROLL falls through here but is
               // never actually rendered via this path — see drawWaveform())
      String line = midiNoteName(lastNote);
      switch (currentFx) {
        case FX_TREMOLO: line += " tremolo"; break;
        case FX_DELAY:   line += " delay"; break;
        case FX_REVERB:  line += " reverb"; break;
        default: break;
      }
      return line;
    }
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[multi-synth-oled] connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[multi-synth-oled] WiFi up, IP=");
  Serial.println(WiFi.localIP());
}

// Same mechanism as esp_note_player.ino/bmp180_wifi.ino — see either for
// the full reasoning. Falls back to DEFAULT_RELAY_HOST/PORT on any failure.
void fetchRelayConfig() {
  relayHost = DEFAULT_RELAY_HOST;
  relayPort = DEFAULT_RELAY_PORT;

  WiFiClientSecure httpsClient;
  httpsClient.setInsecure();

  HTTPClient http;
  String url = String(HAPPYVIEW_URL) + "/xrpc/music.atproto.noizetoyz.synth.listRelayConfig?limit=10";
  Serial.print("[multi-synth-oled] fetching relay config: ");
  Serial.println(url);

  if (!http.begin(httpsClient, url)) {
    Serial.println("[multi-synth-oled] relayConfig fetch: begin() failed, using default");
    return;
  }
  http.addHeader("X-Client-Key", HAPPYVIEW_CLIENT_KEY);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[multi-synth-oled] relayConfig fetch: HTTP ");
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
    Serial.print("[multi-synth-oled] relayConfig fetch: JSON parse failed (");
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
      newestPort = record["synthBroadcastPort"] | DEFAULT_RELAY_PORT;
    }
  }

  if (newestHost.length() > 0) {
    relayHost = newestHost;
    relayPort = newestPort;
    Serial.print("[multi-synth-oled] relay config from ATProto: ");
    Serial.print(relayHost);
    Serial.print(":");
    Serial.println(relayPort);
  } else {
    Serial.println("[multi-synth-oled] relayConfig fetch: no records found, using default");
  }
}

void connectDownlink() {
  Serial.println("[multi-synth-oled] connecting to relay downlink...");
  if (downlink.connect(relayHost.c_str(), relayPort)) {
    Serial.println("[multi-synth-oled] downlink connected");
  } else {
    Serial.println("[multi-synth-oled] downlink connect failed, will retry");
  }
}

void startRickroll() {
  currentMode = MODE_RICKROLL;
  rickrollStep = -1;
  rickrollStepAt = millis();
  Serial.println("[multi-synth-oled] rickroll easter egg triggered");
}

void startRickrollStep() {
  rickrollStep++;
  if (rickrollStep >= NUM_STEPS) {
    // played through once — back to idle, not an infinite loop like the
    // standalone mozzi_rickroll_chime.ino demo
    currentMode = MODE_TONE;
    sounding = false;
    footerX = 64; // reset the marquee so the normal footer text re-enters cleanly
    return;
  }
  if (MELODY[rickrollStep].note == REST) {
    sounding = false;
  } else {
    aSin.setFreq(midiToFreq(MELODY[rickrollStep].note));
    lastNote = MELODY[rickrollStep].note;
    noteStartAt = millis();
    sounding = true;
  }
  rickrollStepAt = millis();
}

void updateRickroll() {
  unsigned long elapsed = millis() - rickrollStepAt;
  unsigned int slotMs = (unsigned int)(MELODY[rickrollStep].dur16 * RICKROLL_MS_PER_16TH);
  bool inGap = sounding && slotMs > RICKROLL_GAP_MS && elapsed >= (slotMs - RICKROLL_GAP_MS);
  if (inGap) sounding = false;
  if (elapsed < slotMs) return;
  startRickrollStep();
}

// Parses one wire line ("note=60 velocity=100 mode=fold foldGain=90 ..."),
// same format as every other device in this station — see
// synth_relay.py's record_to_line(). Collects every field this sketch
// cares about before dispatching, since which fields matter depends on
// mode/synthType. Identical to esp_multi_synth.ino's applyLine(), plus
// bookkeeping (lastNote/noteStartAt/lastScrubPos/lastCutoffHz/
// lastResonance/fmRatioNorm) so the OLED can show what's playing.
void applyLine(const String &line) {
  int note = -1;
  String synthType, mode, sampleId, fxType;
  int velocity = 100, fx = 0, scrubPos = 64, gain = 64, bias = 64;
  int cutoffHzVal = 800, resonanceVal = 64;

  int start = 0;
  while (start < (int)line.length()) {
    int space = line.indexOf(' ', start);
    if (space == -1) space = line.length();
    String pair = line.substring(start, space);
    int eq = pair.indexOf('=');
    if (eq != -1) {
      String key = pair.substring(0, eq);
      String value = pair.substring(eq + 1);
      if (key == "note") note = value.toInt();
      else if (key == "velocity") velocity = value.toInt();
      else if (key == "synthType") synthType = value;
      else if (key == "mode") mode = value;
      else if (key == "sampleId") sampleId = value;
      else if (key == "fxType") fxType = value;
      else if (key == "fxAmount") fx = value.toInt();
      else if (key == "scrubPos") scrubPos = value.toInt();
      else if (key == "foldGain") gain = value.toInt();
      else if (key == "foldBias") bias = value.toInt();
      else if (key == "cutoffHz") cutoffHzVal = value.toInt();
      else if (key == "resonance") resonanceVal = value.toInt();
    }
    start = space + 1;
  }

  if (synthType == "rickroll-easteregg") {
    startRickroll();
    return;
  }

  if (note < 0 && mode != "scrub") return; // scrub doesn't need a note

  if (mode == "scrub") {
    if (sampleId == "bamboo01") currentSample = SAMPLE_BAMBOO01;
    else if (sampleId == "raven") currentSample = SAMPLE_RAVEN;
    else currentSample = SAMPLE_BAMBOO00;

    long target = map(constrain(scrubPos, 0, 127), 0, 127, 0, numCellsForSample(currentSample) - 1);
    scrub.set(Q16n0_to_Q16n16(target), AUDIO_STEPS_PER_CONTROL);
    currentMode = MODE_SCRUB;
    lastScrubPos = constrain(scrubPos, 0, 127);
    noteStartAt = millis(); // re-pulse the waveform's decay envelope on every scrub update
    Serial.print("[multi-synth-oled] scrub sampleId=");
    Serial.print(sampleId);
    Serial.print(" scrubPos=");
    Serial.println(scrubPos);
  } else if (mode == "fold") {
    aSin.setFreq(midiToFreq(note));
    foldGain = constrain(gain, 0, 127);
    foldBias = constrain(bias, 0, 127);
    sounding = true;
    noteOffAt = millis() + NOTE_DURATION_MS;
    currentMode = MODE_FOLD;
    lastNote = note;
    noteStartAt = millis();
    Serial.print("[multi-synth-oled] fold note=");
    Serial.print(note);
    Serial.print(" foldGain=");
    Serial.print(foldGain);
    Serial.print(" foldBias=");
    Serial.println(foldBias);
  } else if (mode == "filter") {
    aSaw.setFreq(midiToFreq(note));
    // the filter's own cutoff/resonance range is a byte (0-255, per its
    // own example) — map incoming Hz/0-127 values onto it, approximate
    // rather than a precise Hz match.
    uint8_t cutoffByte = constrain(map(constrain(cutoffHzVal, 0, 3800), 0, 3800, 0, 255), 0, 255);
    uint8_t resonanceByte = constrain(map(constrain(resonanceVal, 0, 127), 0, 127, 0, 255), 0, 255);
    rf.setCutoffFreqAndResonance(cutoffByte, resonanceByte);
    sounding = true;
    noteOffAt = millis() + NOTE_DURATION_MS;
    currentMode = MODE_FILTER;
    lastNote = note;
    lastCutoffHz = cutoffHzVal;
    lastResonance = resonanceVal;
    noteStartAt = millis();
    Serial.print("[multi-synth-oled] filter note=");
    Serial.print(note);
    Serial.print(" cutoffHz=");
    Serial.print(cutoffHzVal);
    Serial.print(" resonance=");
    Serial.println(resonanceVal);
  } else if (mode == "fm") {
    // fm reuses foldGain/foldBias's 0-127 slots as fmIndex/fmRatio — same
    // physical fields, different meaning per mode, documented in the
    // lexicon's own _comment.
    float carrierFreq = midiToFreq(note);
    float ratioNorm = 0.5 + (constrain(bias, 0, 127) / 127.0) * 3.5;
    fmIndexNorm = (constrain(gain, 0, 127) / 127.0) * 4.0;
    fmRatioNorm = ratioNorm;
    aFmCarrier.setFreq(carrierFreq);
    aFmModulator.setFreq(carrierFreq * ratioNorm);
    sounding = true;
    noteOffAt = millis() + NOTE_DURATION_MS;
    currentMode = MODE_FM;
    lastNote = note;
    noteStartAt = millis();
    Serial.print("[multi-synth-oled] fm note=");
    Serial.print(note);
    Serial.print(" fmIndex=");
    Serial.print(gain);
    Serial.print(" fmRatio=");
    Serial.println(bias);
  } else if (mode == "pluck") {
    aSin.setFreq(midiToFreq(note));
    unsigned int decayMs = map(constrain(velocity, 0, 127), 0, 127, 80, 900);
    pluckEnvelope.start(5, decayMs);
    sounding = true;
    currentMode = MODE_PLUCK; // no noteOffAt — the Ead envelope itself silences it
    lastNote = note;
    noteStartAt = millis();
    Serial.print("[multi-synth-oled] pluck note=");
    Serial.print(note);
    Serial.print(" decayMs=");
    Serial.println(decayMs);
  } else {
    // mode absent or "tone" — the original, only-ever mode before this sketch
    aSin.setFreq(midiToFreq(note));
    fxAmount = constrain(fx, 0, 100);
    if (fxType == "tremolo") currentFx = FX_TREMOLO;
    else if (fxType == "delay") currentFx = FX_DELAY;
    else if (fxType == "reverb") currentFx = FX_REVERB;
    else currentFx = FX_NONE;
    sounding = true;
    noteOffAt = millis() + NOTE_DURATION_MS;
    currentMode = MODE_TONE;
    lastNote = note;
    noteStartAt = millis();
    Serial.print("[multi-synth-oled] tone note=");
    Serial.print(note);
    Serial.print(" fxType=");
    Serial.print(fxType);
    Serial.print(" fxAmount=");
    Serial.println(fxAmount);
  }
}

void pollDownlink() {
  if (!downlink.connected()) {
    connectDownlink();
    return;
  }
  while (downlink.available()) {
    char c = downlink.read();
    if (c == '\n') {
      applyLine(lineBuffer);
      lineBuffer = "";
    } else if (c != '\r') {
      lineBuffer += c;
    }
  }
}

void drawWaveform() {
  display.clearDisplay();

  // --- info line (y 0-7) ---
  // Rickroll gets a distinct visual beat rather than reading as an ordinary
  // note: the line inverts (filled white rect, black text) and flashes at
  // ~2Hz instead of showing a mode/param summary.
  bool rickrolling = (currentMode == MODE_RICKROLL);
  bool flashOn = rickrolling && ((millis() / 250) % 2 == 0);
  display.setTextSize(1);
  if (flashOn) {
    display.fillRect(0, 0, W, 8, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(0, 0);
  display.print(rickrolling ? "RICKROLL!" : buildInfoLine());

  // --- waveform, decaying envelope (y WAVE_TOP..WAVE_TOP+WAVE_HEIGHT) ---
  // Driven by lastNote for every mode that has one (tone/fold/filter/fm/
  // pluck/rickroll); scrub has no single "note" so it falls back to
  // scrubPos instead. Same decay-envelope shape either way, re-triggered
  // by noteStartAt on every mode's update (see applyLine()).
  float elapsed = millis() - noteStartAt;
  float amplitude = IDLE_AMPLITUDE + (PEAK_AMPLITUDE - IDLE_AMPLITUDE) * exp(-elapsed / DECAY_TAU_MS);
  float freq = (currentMode == MODE_SCRUB) ? scrubToVisualFreq(lastScrubPos) : noteToVisualFreq(lastNote);

  int prevY = -1;
  for (int x = 0; x < W; x++) {
    float y = WAVE_MID + amplitude * sin(2.0 * PI * freq * x + phase);
    int yi = (int)y;
    if (yi < WAVE_TOP) yi = WAVE_TOP;
    if (yi >= WAVE_TOP + WAVE_HEIGHT) yi = WAVE_TOP + WAVE_HEIGHT - 1;

    if (prevY >= 0) {
      display.drawLine(x - 1, prevY, x, yi, WHITE);
    } else {
      display.drawPixel(x, yi, WHITE);
    }
    prevY = yi;
  }

  // --- footer marquee (y 40-47) ---
  const char *footerText = rickrolling ? RICKROLL_FOOTER_TEXT : FOOTER_TEXT;
  display.setTextColor(WHITE);
  display.setCursor((int)footerX, 40);
  display.print(footerText);

  display.display();

  phase += PHASE_STEP;

  int footerWidth = strlen(footerText) * 6; // 6px/char at text size 1 (5px glyph + 1px spacing)
  footerX -= FOOTER_STEP;
  if (footerX < -footerWidth) {
    footerX = W;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[multi-synth-oled] starting");

  Wire.begin(D2, D1); // SDA, SCL
  Wire.setClockStretchLimit(1000); // microseconds — see bmp180_smoke_test.ino's REAL-HARDWARE FINDING note; the classic ESP8266 Wire library has no timeout by default and a stuck/miswired I2C bus hangs forever without this

  Wire.beginTransmission(0x3C);
  uint8_t oledResult = Wire.endTransmission();
  if (oledResult != 0) {
    while (1) {
      Serial.print("[multi-synth-oled] no ACK from OLED at 0x3C (endTransmission=");
      Serial.print(oledResult);
      Serial.println(") — check the OLED shield is fully seated on the stacking header");
      delay(1000);
    }
  }

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  aSampleBamboo00.setLoopingOn();
  aSampleBamboo01.setLoopingOn();
  aSampleRaven.setLoopingOn();
  wf.setLimits(-2047, 2047); // 12-bit output range, matches WaveFolder example
  aDelay.setFeedbackLevel(90); // a repeating slapback rather than the AudioDelayFeedback example's flange-y negative feedback

  connectWiFi();
  fetchRelayConfig();
  connectDownlink();

  // Same startup-race fix as every Mozzi+ESP8266 sketch in this station —
  // see diagnostics/mozzi_startup_race_repro/.
  Serial.flush();
  delay(500);

  startMozzi();
  Serial.println("[multi-synth-oled] Mozzi started");
}

void updateControl() {
  pollDownlink();

  if (currentMode == MODE_RICKROLL) {
    updateRickroll();
  } else {
    bool gatedMode = currentMode == MODE_TONE || currentMode == MODE_FOLD ||
                     currentMode == MODE_FILTER || currentMode == MODE_FM;
    if (gatedMode && sounding && millis() > noteOffAt) {
      sounding = false;
    }
    // MODE_PLUCK and MODE_SCRUB aren't gated here — the Ead envelope silences
    // pluck on its own, and scrub is a continuous control, not a note-on.
  }

  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawWaveform();
  }
}

AudioOutput updateAudio() {
  switch (currentMode) {
    case MODE_SCRUB: {
      unsigned int index = Q16n16_to_Q16n0(scrub.next());
      int8_t s;
      switch (currentSample) {
        case SAMPLE_BAMBOO01: s = aSampleBamboo01.atIndex(index); break;
        case SAMPLE_RAVEN:    s = aSampleRaven.atIndex(index); break;
        default:              s = aSampleBamboo00.atIndex(index); break;
      }
      return MonoOutput::from8Bit(s);
    }
    case MODE_FOLD: {
      if (!sounding) return MonoOutput::from8Bit(0);
      int8_t gain = foldGain;
      int8_t bias = (int8_t)foldBias - 64;
      int sample = (gain * aSin.next() >> 1) + (bias << 4);
      return MonoOutput::fromNBit(12, wf.next(sample));
    }
    case MODE_FILTER: {
      if (!sounding) return MonoOutput::from8Bit(0);
      char filtered = rf.next(aSaw.next());
      return MonoOutput::from8Bit(filtered);
    }
    case MODE_FM: {
      if (!sounding) return MonoOutput::from8Bit(0);
      float modOut = fmIndexNorm * (aFmModulator.next() / 128.0);
      Q15n16 modulation = float_to_Q15n16(modOut);
      return MonoOutput::from8Bit(aFmCarrier.phMod(modulation));
    }
    case MODE_PLUCK: {
      int gain = pluckEnvelope.next();
      return MonoOutput::from16Bit(gain * aSin.next());
    }
    case MODE_RICKROLL:
      if (!sounding) return MonoOutput::from8Bit(0);
      return MonoOutput::from8Bit(aSin.next());
    default: { // MODE_TONE
      if (!sounding) return MonoOutput::from8Bit(0);
      int8_t sample = aSin.next();
      switch (currentFx) {
        case FX_TREMOLO: {
          if (fxAmount > 0) {
            float depth = fxAmount / 100.0;
            float mod = 1.0 - depth + depth * (0.5 + 0.5 * sin(millis() * 0.01));
            sample = (int8_t)(sample * mod);
          }
          return MonoOutput::from8Bit(sample);
        }
        case FX_DELAY: {
          uint16_t delSamps = map(constrain(fxAmount, 0, 100), 0, 100, 10, 120);
          return MonoOutput::fromAlmostNBit(9, (sample >> 3) + aDelay.next(sample, delSamps));
        }
        case FX_REVERB:
          return MonoOutput::fromNBit(16, reverb.next(sample));
        default:
          return MonoOutput::from8Bit(sample);
      }
    }
  }
}

void loop() {
  audioHook();
}
