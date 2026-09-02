/*  Station 5 — receive-only instrument for every player.html mode: plain
    tone+fx, sample scrubbing, wavefolding, a resonant filter sweep, FM
    synthesis, and a plucked-envelope voice — plus a hidden rickroll
    easter egg. Sibling to esp_note_player.ino, not an edit to it: that
    sketch stays the plain tone+OLED demo; this one is what actually plays
    every mode landing-page/player.html's widgets can send.

    Modes added 2026-09-02 in two passes: tone/scrub/fold first (alongside
    the mode/sampleId/scrubPos/foldGain/foldBias fields on
    music.atproto.noizetoyz.synth.note), then filter/fm/pluck plus real
    delay/reverb fx after a full survey of every Mozzi example and
    top-level fx/filter header (`~/src/Mozzi/examples/`, `~/src/Mozzi/*.h`)
    turned up real classes for all of these rather than needing anything
    hand-rolled. Only one further lexicon field needed: `resonance` — `fm`
    reuses `foldGain`/`foldBias` as `fmIndex`/`fmRatio` (documented in the
    lexicon's own `_comment`), and `pluck` needs no new field at all
    (driven by `note`/`velocity` alone).

    Reuses proven pieces rather than re-deriving them:
      - Relay discovery (fetchRelayConfig), downlink connect/reconnect, and
        the manual key=value line parser are copied from
        esp_note_player.ino (minus its OLED — this sketch is audio-only).
      - Tone mode's tremolo trick is copied from esp_synth.ino. Its
        delay/reverb fx (real Mozzi classes now, not placeholders — see
        the fxType dispatch in updateAudio()) are new: `delay` follows
        ~/src/Mozzi/examples/09.Delays/AudioDelayFeedback, `reverb` follows
        ~/src/Mozzi/examples/09.Delays/ReverbTank_STANDARD (ReverbTank was
        specifically sized to fit an Arduino Nano's 2KB RAM, so it costs
        nothing worth worrying about on an ESP8266's 80KB).
      - Scrub mode follows ~/src/Mozzi/examples/08.Samples/Sample_Scrub
        (Sample<> + Line<Q16n16>), scrub target from the network's
        scrubPos instead of local wandering/randomness.
      - Fold mode follows ~/src/Mozzi/examples/06.Synthesis/WaveFolder
        (WaveFolder<>), gain/bias from the network instead of local LFOs.
        WaveFolder.h ships inside the Mozzi library itself — no extra
        library install needed.
      - Filter mode follows ~/src/Mozzi/examples/10.Audio_Filters/
        ResonantFilter (a sawtooth through `LowPassFilter`/
        `ResonantFilter<uint8_t,LOWPASS>`), finally giving the lexicon's
        long-unused `cutoffHz` field a real job. The filter's own
        cutoff/resonance range is a byte (0-255, per its own example's
        comment), so incoming Hz/0-127 values are mapped onto it — an
        approximation, not a precise Hz match.
      - FM mode follows ~/src/Mozzi/examples/06.Synthesis/FMsynth
        (`Oscil::phMod()`), simplified to plain floats rather than the
        example's full fixed-point deviation math — consistent with this
        sketch's tone-mode tremolo already doing the same tradeoff, and
        Mozzi ships `float_to_Q15n16()` specifically for this bridge.
      - Pluck mode follows ~/src/Mozzi/examples/07.Envelopes/Ead_Envelope
        (`Ead`, the cheapest real envelope Mozzi has), velocity scaling
        decay time — no fixed note-off timer, the envelope itself silences
        it.
      - The rickroll easter egg plays the exact MELODY[] table from
        mozzi_rickroll_chime.ino, once through, then returns to idle —
        triggered by synthType=="rickroll-easteregg" regardless of mode.

    Sample tables (scrub mode): three of Mozzi's own bundled int8
    wavetables, picked for ESP8266 flash budget — two ~8KB `bamboo` tables
    (the smallest Mozzi ships) plus the ~34KB raven table for a bigger/
    different timbre. All three are plain (non-Huffman) tables, same
    simple atIndex() access as the Sample_Scrub example.

    Audio out: fixed GPIO2/D4 on ESP8266, same as every sketch in this
    station. UNTESTED as a whole — assembled from already-verified pieces,
    not independently confirmed as this exact combination on real hardware.
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

// ---- tone + fold carrier ----
Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSin(SIN2048_DATA);

// ---- fold ----
WaveFolder<> wf;
uint8_t foldGain = 64;
uint8_t foldBias = 64;

// ---- filter ----
Oscil<SAW2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSaw(SAW2048_DATA);
LowPassFilter rf; // ResonantFilter<uint8_t, LOWPASS>

// ---- fm ----
Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aFmCarrier(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aFmModulator(SIN2048_DATA);
float fmIndexNorm = 0.5; // 0..~1.5 cycles of phase deviation — kept modest since Mozzi's
                          // own FMsynth example warns aliasing audibly intrudes at higher
                          // deviation on its 16384Hz sample rate; the original 0..4 range
                          // here was untested and likely too aggressive

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

long numCellsForSample(SampleChoice s) {
  switch (s) {
    case SAMPLE_BAMBOO00: return BAMBOO_00_2048_NUM_CELLS;
    case SAMPLE_BAMBOO01: return BAMBOO_01_2048_NUM_CELLS;
    case SAMPLE_RAVEN:    return RAVEN_ARH_NUM_CELLS;
  }
  return BAMBOO_00_2048_NUM_CELLS;
}

// ---- rickroll easter egg — MELODY[] copied verbatim from
// mozzi_rickroll_chime.ino; see that file for the transcription's
// provenance/verification. ----
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

float midiToFreq(int note) {
  return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[multi-synth] connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[multi-synth] WiFi up, IP=");
  Serial.println(WiFi.localIP());

  // Real bug, found by ear (2026-09-02): every mode crackled, not just
  // reverb — this is the first sketch in this station to run WiFi and
  // Mozzi audio at the same time (the earlier clean-sounding chimes had
  // no WiFi code at all). ESP8266's default WiFi modem-sleep power saving
  // periodically stalls the radio for tens of milliseconds at a time,
  // which is long enough to disrupt Mozzi's audio-rate timing and is a
  // well-documented cause of exactly this kind of crackle. This sketch is
  // always-on and cares about audio smoothness, not battery life, so
  // there's no downside to disabling it.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
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
  Serial.print("[multi-synth] fetching relay config: ");
  Serial.println(url);

  if (!http.begin(httpsClient, url)) {
    Serial.println("[multi-synth] relayConfig fetch: begin() failed, using default");
    return;
  }
  http.addHeader("X-Client-Key", HAPPYVIEW_CLIENT_KEY);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[multi-synth] relayConfig fetch: HTTP ");
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
    Serial.print("[multi-synth] relayConfig fetch: JSON parse failed (");
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
    Serial.print("[multi-synth] relay config from ATProto: ");
    Serial.print(relayHost);
    Serial.print(":");
    Serial.println(relayPort);
  } else {
    Serial.println("[multi-synth] relayConfig fetch: no records found, using default");
  }
}

void connectDownlink() {
  Serial.println("[multi-synth] connecting to relay downlink...");
  if (downlink.connect(relayHost.c_str(), relayPort)) {
    Serial.println("[multi-synth] downlink connected");
  } else {
    Serial.println("[multi-synth] downlink connect failed, will retry");
  }
}

void startRickroll() {
  currentMode = MODE_RICKROLL;
  rickrollStep = -1;
  rickrollStepAt = millis();
  Serial.println("[multi-synth] rickroll easter egg triggered");
}

void startRickrollStep() {
  rickrollStep++;
  if (rickrollStep >= NUM_STEPS) {
    // played through once — back to idle, not an infinite loop like the
    // standalone mozzi_rickroll_chime.ino demo
    currentMode = MODE_TONE;
    sounding = false;
    return;
  }
  if (MELODY[rickrollStep].note == REST) {
    sounding = false;
  } else {
    aSin.setFreq(midiToFreq(MELODY[rickrollStep].note));
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
// mode/synthType.
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
    Serial.print("[multi-synth] scrub sampleId=");
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
    Serial.print("[multi-synth] fold note=");
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
    Serial.print("[multi-synth] filter note=");
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
    float fmRatioNorm = 0.5 + (constrain(bias, 0, 127) / 127.0) * 3.5;
    fmIndexNorm = (constrain(gain, 0, 127) / 127.0) * 1.5;
    aFmCarrier.setFreq(carrierFreq);
    aFmModulator.setFreq(carrierFreq * fmRatioNorm);
    sounding = true;
    noteOffAt = millis() + NOTE_DURATION_MS;
    currentMode = MODE_FM;
    Serial.print("[multi-synth] fm note=");
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
    Serial.print("[multi-synth] pluck note=");
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
    Serial.print("[multi-synth] tone note=");
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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[multi-synth] starting");

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
  Serial.println("[multi-synth] Mozzi started");
}

void updateControl() {
  pollDownlink();

  if (currentMode == MODE_RICKROLL) {
    updateRickroll();
    return;
  }

  bool gatedMode = currentMode == MODE_TONE || currentMode == MODE_FOLD ||
                   currentMode == MODE_FILTER || currentMode == MODE_FM;
  if (gatedMode && sounding && millis() > noteOffAt) {
    sounding = false;
  }
  // MODE_PLUCK and MODE_SCRUB aren't gated here — the Ead envelope silences
  // pluck on its own, and scrub is a continuous control, not a note-on.
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
        case FX_REVERB: {
          // Real bug, found by ear (2026-09-02): this used to be
          // fromNBit(16, reverb.next(sample)) with no dry signal and no
          // attenuation — reverb.next()'s raw output isn't a clean 16-bit
          // range on its own, so that clipped hard and came out as
          // crackle/noise, not reverb. Matches
          // ~/src/Mozzi/examples/09.Delays/ReverbTank_STANDARD exactly now:
          // mix dry + attenuated wet, fromAlmostNBit(9, ...).
          int arev = reverb.next(sample);
          return MonoOutput::fromAlmostNBit(9, sample + (arev >> 3));
        }
        default:
          return MonoOutput::from8Bit(sample);
      }
    }
  }
}

void loop() {
  audioHook();
}
