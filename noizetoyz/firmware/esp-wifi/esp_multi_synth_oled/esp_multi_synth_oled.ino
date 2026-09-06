// CONFIRMED ON REAL HARDWARE (2026-09-02): pitch was stuck regardless of
// note/mode on this sketch specifically, while the identical dispatch/
// setFreq code is verified correct on esp_multi_synth.ino (no OLED) — the
// application logic is byte-for-byte the same, so the cause was the OLED's
// I2C write (display.display(), ~384 bytes) interfering with Mozzi's
// audio-rate timer closely enough to corrupt the oscillator's frequency
// state, not just cause crackle. Confirmed by flipping this to 1 (zero
// display updates) and pitch tracked correctly.
//
// Real fix (2026-09-03), confirmed on hardware: a full 384-byte I2C write
// at the ESP8266's 100kHz default clock takes roughly 30ms+ — enough to
// stall Mozzi's audio-rate timer for hundreds of samples. Bumped I2C to
// 400kHz Fast Mode (setup(), ~4x shorter transfer) and halved the redraw
// rate to 10fps (DRAW_INTERVAL_MS) so the shorter stall also happens half
// as often. Both pitch and display confirmed working together with this.
// Flip back to 1 immediately if pitch ever regresses — don't assume this
// holds on different hardware without re-confirming by ear.
#define DIAG_DISABLE_OLED_DRAW 0

// Set to 1 to skip the real relayConfig fetch and always take the
// DEFAULT_RELAY_HOST fallback path — for testing the fallback icon/
// behavior on demand rather than needing to actually break HappyView
// reachability. Flip back to 0 before leaving it running unattended.
#define FORCE_RELAY_FALLBACK 0

/*  Noizetoyz — esp_multi_synth.ino's full mode dispatch (tone+fx, scrub,
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
      - The waveform band is mode-specific (drawWaveBand(), added
        2026-09-03 once pitch+display were both confirmed working
        together) — scrub gets a literal scanning read-head over a
        wavetable track, fold actually folds the drawn sine past a
        foldGain-driven threshold, filter draws a real resonance peak at
        the actual cutoffHz position, fm layers a fmRatioNorm/fmIndexNorm-
        driven ripple on the carrier — not just palette-swapped copies of
        one generic waveform. tone/pluck keep the original decaying-
        envelope sine, still the right shape for "a note was struck."
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

// ---- multi-sensor shields, added 2026-09-04 ----
// This board physically stacks a BMP180 shield (I2C, addr 0x77 — no
// conflict with the OLED's 0x3C on the same D1/D2 bus) alongside the
// synth+OLED hardware, plus free GPIO for a DHT22 added later (D5, same
// pin/pull-up convention as dht22_wifi.ino/ds18b20_test.ino). Both are
// genuinely optional: a missing/failed BMP180 or an unwired DHT22 just
// leaves that sensor's icon dark, it never blocks the synth from working.
#include <Adafruit_BMP085.h>
#include <DHT.h>
#define BMP180_I2C_ADDR 0x77
#define DHT_PIN D5
#define DHT_TYPE DHT22
Adafruit_BMP085 bmp;
DHT dht(DHT_PIN, DHT_TYPE);

// ---- fill in for your workshop WiFi ----
const char *WIFI_SSID = "noizetoyz";
const char *WIFI_PASSWORD = "synthbeep";
// ------------------------------------------------------

// Relay location discovered via ATProto at boot — see esp_note_player.ino's
// identical fetchRelayConfig() for the full reasoning. Only the fallback
// if that fetch fails.
const char *DEFAULT_RELAY_HOST = "192.168.1.20";
const uint16_t DEFAULT_RELAY_PORT = 8479; // synth_relay.py's SYNTH_BROADCAST_PORT
const uint16_t SENSOR_TCP_PORT = 8480; // wifi_sensor_relay.py's own port — see bmp180_wifi.ino/dht22_wifi.ino

const char *HAPPYVIEW_URL = "https://happyview.werk.museum";
const char *HAPPYVIEW_CLIENT_KEY = "hvc_4f63d844f5a253fe658f1491160126dc";

String relayHost;
uint16_t relayPort;
uint16_t sensorRelayPort; // sensorTcpPort from the same relayConfig record — a different port
                          // on the same relayHost, live-data's wifi_sensor_relay.py rather than
                          // synth_relay.py's downlink; see fetchRelayConfig()'s parsing loop
WiFiClient downlink;
String lineBuffer;
bool relayFromATProto = false; // set by fetchRelayConfig() — false means DEFAULT_RELAY_HOST was
                                // used, the exact silent failure mode diagnosed 2026-09-03
                                // (ESP8266 BearSSL default buffer sizes starving the HTTPS fetch;
                                // see fetchRelayConfig()'s own setBufferSizes() line). The status
                                // HUD surfaces this live instead of it only showing up in Serial.

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
// Screen layout — 64x48 total, split into three bands, same as originally:
//   y  0- 7: per-mode info line (text size 1, 8px tall) — see buildInfoLine()
//   y 10-37: waveform, in its own 28px band
//   y 40-47: scrolling footer marquee (text size 1, 8px tall) — the status
//            icon badge (drawStatusHud()) now lives here too, opaque-masked
//            over the left edge of the marquee, rather than in its own row
//            stealing height from the waveform (tried 2026-09-03, reverted
//            same day — the waveform is the whole point of this display).
const int W = 64;
const int FOOTER_Y = 40;
const int WAVE_TOP = 10;
const int WAVE_HEIGHT = 28;
const int WAVE_MID = WAVE_TOP + WAVE_HEIGHT / 2;

// ---- status HUD icons, 8x8 monochrome, drawn with Adafruit_GFX's
// drawBitmap() — one byte per row, MSB-first. Kept deliberately simple/
// geometric rather than skeuomorphic: legible at 8px is the only bar.
// A WiFi icon was here too originally but got dropped 2026-09-03 — it was
// drawn as a static "connected" glyph that never re-checked WiFi.status(),
// so it never actually changed after boot and told the viewer nothing.
// The two icons below are both genuinely live.
const uint8_t ICON_HEART[] PROGMEM = { // relay config: fetched from ATProto/HappyView
  0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
};
const uint8_t ICON_FALLBACK[] PROGMEM = { // relay config: DEFAULT_RELAY_HOST used instead —
  0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x18  // reads as an exclamation mark on real hardware
};
const uint8_t ICON_LINK[] PROGMEM = { // downlink: connected
  0x00, 0x66, 0x99, 0x99, 0x99, 0x99, 0x66, 0x00
};
const uint8_t ICON_LINK_BROKEN[] PROGMEM = { // downlink: not connected
  0x00, 0x60, 0x90, 0x90, 0x09, 0x09, 0x06, 0x00
};
const uint8_t ICON_NOTE_IN[] PROGMEM = { // a note just arrived over the downlink
  0x00, 0x06, 0x05, 0x04, 0x04, 0x64, 0x94, 0x60
};
const uint8_t ICON_PRESSURE[] PROGMEM = { // a barometer dial — new 2026-09-04, no prior draft existed
  0x3C, 0x42, 0x81, 0x89, 0x91, 0x81, 0x42, 0x3C
};
const uint8_t ICON_TEMPERATURE[] PROGMEM = { // thermometer — same bytes drafted in the Noizetoys icon sheet
  0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x7E, 0x7E
};
const uint8_t ICON_HUMIDITY[] PROGMEM = { // droplet — same bytes drafted in the Noizetoys icon sheet
  0x18, 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C
};
const int ICON_W = 8;
const int ICON_H = 8;
const int ICON_GAP = 2;
const int ICON_ZONE_W = 2 * ICON_W + ICON_GAP; // the two-icon badge's total footprint
const int ICON_ZONE_PAD = 3; // extra masked gap between the badge and the scrolling text

// Incoming-data activity row — top-right corner, 4 icons wide, dedicated
// 2026-09-04. This board physically carries a BMP180 shield (pressure +
// temperature) and free GPIO for a DHT22 (humidity) added later, on top
// of its own downlink (anyone's note, via synth_relay.py's Jetstream
// re-broadcast) — four independent "something just happened" signals,
// the same idea as a network activity LED, grouped together since
// they're all activity indicators. Right to left: note-in, humidity,
// temperature, pressure — see drawStatusIconRow(). Each sensor icon has
// two states rather than one: lit steady once that sensor is confirmed
// present (bmp180Present / humidityPresent), and a brief *blink-off*
// (not on) the instant a fresh reading comes in — distinct from the
// note-in icon's flash-on, chosen because "present" is the steady-state
// fact worth always showing, and a reading is a transient event against
// that steady background.
unsigned long lastNoteInAt = 0;
const unsigned long NOTE_IN_FLASH_MS = 400; // long enough to actually see, short enough to read as "just now"

bool bmp180Present = false; // checked once at boot — a missing/failed shield just leaves these icons dark
bool humidityPresent = false; // sticky true on the first good DHT22 read — not yet installed as of 2026-09-04
unsigned long lastPressureReadAt = 0;
unsigned long lastTemperatureReadAt = 0;
unsigned long lastHumidityReadAt = 0;
const unsigned long SENSOR_BLINK_OFF_MS = 150; // brief enough to read as a blink, not a dropout
const unsigned long SENSOR_READ_INTERVAL_MS = 5000; // matches bmp180_wifi.ino/dht22_wifi.ino's own SEND_INTERVAL_MS
unsigned long lastSensorReadAt = 0;
const char *SENSOR_DEVICE_ID = "d1mini-synth-oled-sensors"; // distinct from bmp180_wifi.ino/dht22_wifi.ino's own standalone-board device IDs

// Status icon badge — which relay-config source is active (real ATProto/
// HappyView discovery vs the hardcoded fallback), and live downlink
// connection state. Right-aligned in the footer row, opaque-masked (with
// a little breathing room) over the scrolling marquee text — see the
// fillRect call right before this at the call site — rather than owning
// a row of its own; a dedicated row was tried 2026-09-03 and reverted the
// same session, it ate into the waveform for little gain.
void drawStatusHud() {
  int x = W - ICON_ZONE_W;
  display.drawBitmap(x, FOOTER_Y, relayFromATProto ? ICON_HEART : ICON_FALLBACK, ICON_W, ICON_H, WHITE);
  x += ICON_W + ICON_GAP;
  display.drawBitmap(x, FOOTER_Y, downlink.connected() ? ICON_LINK : ICON_LINK_BROKEN, ICON_W, ICON_H, WHITE);
}

// Incoming-data row, y=0, right to left: note-in, humidity, temperature,
// pressure. Every slot is masked black unconditionally first (so
// buildInfoLine() text — several mode strings already run close to the
// full 64px width — never bleeds into a reserved slot even when that
// slot's icon isn't currently drawn), then the icon is drawn on top only
// when applicable. Sensor icons: lit once bmp180Present/humidityPresent
// (checked once at boot / on first good read), blinked *off* briefly
// right after a fresh reading — see SENSOR_BLINK_OFF_MS.
void drawStatusIconRow() {
  int x = W - ICON_W;
  display.fillRect(x, 0, ICON_W, ICON_H, BLACK);
  if (millis() - lastNoteInAt < NOTE_IN_FLASH_MS) {
    display.drawBitmap(x, 0, ICON_NOTE_IN, ICON_W, ICON_H, WHITE);
  }

  x -= ICON_W;
  display.fillRect(x, 0, ICON_W, ICON_H, BLACK);
  if (humidityPresent && millis() - lastHumidityReadAt >= SENSOR_BLINK_OFF_MS) {
    display.drawBitmap(x, 0, ICON_HUMIDITY, ICON_W, ICON_H, WHITE);
  }

  x -= ICON_W;
  display.fillRect(x, 0, ICON_W, ICON_H, BLACK);
  if (bmp180Present && millis() - lastTemperatureReadAt >= SENSOR_BLINK_OFF_MS) {
    display.drawBitmap(x, 0, ICON_TEMPERATURE, ICON_W, ICON_H, WHITE);
  }

  x -= ICON_W;
  display.fillRect(x, 0, ICON_W, ICON_H, BLACK);
  if (bmp180Present && millis() - lastPressureReadAt >= SENSOR_BLINK_OFF_MS) {
    display.drawBitmap(x, 0, ICON_PRESSURE, ICON_W, ICON_H, WHITE);
  }
}

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
const unsigned long DRAW_INTERVAL_MS = 100; // ~10fps — halved from the original 20fps alongside the
                                             // 400kHz I2C bump, so the (still blocking) redraw happens
                                             // half as often on top of taking a quarter as long each time
const unsigned long BOOT_STATUS_HOLD_MS = 1400; // how long each boot-status message stays up before
                                                 // the next one overwrites it. 600ms (the original
                                                 // value) was confirmed too fast to actually read on
                                                 // real hardware 2026-09-03 — bumped until it's
                                                 // comfortable, at the cost of ~6s added to every boot.

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

  // Same fix as esp_multi_synth.ino, found by ear on real hardware
  // 2026-09-02: ESP8266's default WiFi modem-sleep periodically stalls
  // the radio for tens of milliseconds, long enough to disrupt Mozzi's
  // audio-rate timing and cause audible crackle across every mode. This
  // sketch is always-on and cares about audio smoothness, not battery
  // life, so there's no downside to disabling it.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
}

// Same mechanism as esp_note_player.ino/bmp180_wifi.ino — see either for
// the full reasoning. Falls back to DEFAULT_RELAY_HOST/PORT on any failure.
void fetchRelayConfig() {
  relayHost = DEFAULT_RELAY_HOST;
  relayPort = DEFAULT_RELAY_PORT;
  sensorRelayPort = SENSOR_TCP_PORT;
  relayFromATProto = false;

#if FORCE_RELAY_FALLBACK
  Serial.println("[multi-synth-oled] FORCE_RELAY_FALLBACK set — skipping ATProto fetch");
  return;
#endif

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
  uint16_t newestSensorPort = SENSOR_TCP_PORT;

  for (JsonObject record : records) {
    const char *createdAt = record["createdAt"] | "";
    const char *host = record["relayHost"] | "";
    if (strlen(host) == 0) continue;
    if (String(createdAt) > newestCreatedAt) {
      newestCreatedAt = createdAt;
      newestHost = host;
      newestPort = record["synthBroadcastPort"] | DEFAULT_RELAY_PORT;
      newestSensorPort = record["sensorTcpPort"] | SENSOR_TCP_PORT;
    }
  }

  if (newestHost.length() > 0) {
    relayHost = newestHost;
    relayPort = newestPort;
    sensorRelayPort = newestSensorPort;
    relayFromATProto = true;
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

// Fresh short-lived connection per send, one write, matching
// bmp180_wifi.ino's sendReading() exactly — see that file's own
// REAL-HARDWARE FINDING note: multiple client.print() calls followed
// immediately by client.stop() truncated messages on this platform.
void sendSensorReading(const String &line) {
  WiFiClient client;
  if (!client.connect(relayHost.c_str(), sensorRelayPort)) {
    Serial.println("[multi-synth-oled] sensor relay connect failed");
    return;
  }
  client.print(line + "\n");
  client.flush();
  client.stop();
  Serial.print("[multi-synth-oled] sensor sent: ");
  Serial.print(line);
}

// Throttled to SENSOR_READ_INTERVAL_MS from updateControl() — never called
// every control tick. Even so, a BMP180 read (several I2C transactions)
// costs tens of ms of blocking time once per interval, and a DHT22 read
// costs ~250ms — a real, audible audio stall once DHT22 is actually wired
// in, not yet confirmed against real hardware since it isn't installed
// yet. Worth listening for by ear the same way the OLED redraw itself was
// diagnosed, if this ever gets flashed with a DHT22 present.
void readAndPublishSensors() {
  if (bmp180Present) {
    float tempC = bmp.readTemperature();
    int32_t pressurePa = bmp.readPressure();
    lastPressureReadAt = millis();
    lastTemperatureReadAt = millis();
    String line = "temp=" + String((int)(tempC * 10)) + " pressure=" + String(pressurePa) +
                  " deviceId=" + String(SENSOR_DEVICE_ID);
    sendSensorReading(line);
  }

  float humidity = dht.readHumidity();
  if (!isnan(humidity)) {
    humidityPresent = true;
    lastHumidityReadAt = millis();
    String line = "humidity=" + String((int)(humidity * 10)) + " deviceId=" + String(SENSOR_DEVICE_ID);
    sendSensorReading(line);
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
      lastNoteInAt = millis();
      lineBuffer = "";
    } else if (c != '\r') {
      lineBuffer += c;
    }
  }
}

// Mode-specific waveform-band visuals, added 2026-09-03 once pitch+display
// were both confirmed working together. Each one is a genuine (if
// necessarily simplified, on a 64x28px mono display) depiction of what
// that mode's Mozzi technique actually does, driven by the same live
// parameters buildInfoLine() shows as text — not just a palette-swapped
// copy of the original decaying sine.

// tone/pluck/rickroll: the original decaying-envelope sine — unchanged,
// still the right shape for "a note was struck and is ringing out."
void drawDecayingSineWave() {
  float elapsed = millis() - noteStartAt;
  float amplitude = IDLE_AMPLITUDE + (PEAK_AMPLITUDE - IDLE_AMPLITUDE) * exp(-elapsed / DECAY_TAU_MS);
  float freq = noteToVisualFreq(lastNote);

  int prevY = -1;
  for (int x = 0; x < W; x++) {
    float y = WAVE_MID + amplitude * sin(2.0 * PI * freq * x + phase);
    int yi = constrain((int)y, WAVE_TOP, WAVE_TOP + WAVE_HEIGHT - 1);
    if (prevY >= 0) display.drawLine(x - 1, prevY, x, yi, WHITE);
    else display.drawPixel(x, yi, WHITE);
    prevY = yi;
  }
}

// scrub: a read head sweeping a wavetable track — a static centerline (the
// table) plus a bright vertical tick at the current scrub position, per
// your "scrub could be a scanning line" note. Deliberately abstract rather
// than the real sample's shape — the OLED sketch has no access to the
// actual wavetable data, just scrubPos.
void drawScrubScan() {
  display.drawFastHLine(0, WAVE_MID, W, WHITE);
  int scanX = map(constrain(lastScrubPos, 0, 127), 0, 127, 0, W - 1);
  display.drawFastVLine(scanX, WAVE_TOP, WAVE_HEIGHT, WHITE);
  display.drawFastVLine(constrain(scanX - 1, 0, W - 1), WAVE_TOP + 2, WAVE_HEIGHT - 4, WHITE);
}

// fold: the same sine, but actually folded back on itself past a threshold
// set by foldGain (higher gain = more aggressive fold = lower threshold,
// same direction as the real WaveFolder<> effect) and recentered by
// foldBias — this is a real fold operation on the drawn values, not just a
// different-looking waveform.
void drawFoldedWave() {
  float freq = noteToVisualFreq(lastNote);
  float threshold = (WAVE_HEIGHT / 2.0 - 2.0) * (1.0 - (foldGain / 127.0) * 0.7);
  if (threshold < 2.0) threshold = 2.0;
  float driveGain = 1.0 + foldGain / 24.0; // pushes the raw sine past the threshold so folding is visible
  float biasOffset = (foldBias - 64) / 10.0;

  int prevY = -1;
  for (int x = 0; x < W; x++) {
    float raw = (PEAK_AMPLITUDE * 0.8) * driveGain * sin(2.0 * PI * freq * x + phase);
    for (int i = 0; i < 5 && (raw > threshold || raw < -threshold); i++) {
      if (raw > threshold) raw = 2.0 * threshold - raw;
      if (raw < -threshold) raw = -2.0 * threshold - raw;
    }
    int yi = constrain((int)(WAVE_MID + raw + biasOffset), WAVE_TOP, WAVE_TOP + WAVE_HEIGHT - 1);
    if (prevY >= 0) display.drawLine(x - 1, prevY, x, yi, WHITE);
    else display.drawPixel(x, yi, WHITE);
    prevY = yi;
  }
}

// filter: a resonance peak on a frequency axis — X position is the actual
// cutoffHz mapped across the band, peak height is the actual resonance.
// Reads like a synth's filter-sweep display, not a generic waveform.
void drawFilterSweep() {
  int baseline = WAVE_TOP + WAVE_HEIGHT - 1;
  display.drawFastHLine(0, baseline, W, WHITE);
  int peakX = map(constrain(lastCutoffHz, 0, 4000), 0, 4000, 0, W - 1);
  int peakHeight = map(constrain(lastResonance, 0, 127), 0, 127, 3, WAVE_HEIGHT - 2);
  int slopeWidth = 10;
  for (int dx = -slopeWidth; dx <= slopeWidth; dx++) {
    int x = peakX + dx;
    if (x < 0 || x >= W) continue;
    float falloff = 1.0 - (float)abs(dx) / slopeWidth;
    int y = baseline - (int)(peakHeight * falloff * falloff);
    display.drawFastVLine(x, constrain(y, WAVE_TOP, baseline), baseline - constrain(y, WAVE_TOP, baseline) + 1, WHITE);
  }
}

// fm: carrier sine plus a faster ripple riding on it — ripple frequency
// tracks fmRatioNorm (the actual modulator:carrier ratio), ripple amplitude
// tracks fmIndexNorm (the actual modulation depth), so a bigger/faster
// squiggle really does mean a more aggressive FM setting.
void drawFmRipple() {
  float freq = noteToVisualFreq(lastNote);
  int prevY = -1;
  for (int x = 0; x < W; x++) {
    float carrier = (PEAK_AMPLITUDE * 0.7) * sin(2.0 * PI * freq * x + phase);
    float ripple = (fmIndexNorm * 3.0) * sin(2.0 * PI * freq * fmRatioNorm * 2.0 * x + phase * 3.0);
    int yi = constrain((int)(WAVE_MID + carrier + ripple), WAVE_TOP, WAVE_TOP + WAVE_HEIGHT - 1);
    if (prevY >= 0) display.drawLine(x - 1, prevY, x, yi, WHITE);
    else display.drawPixel(x, yi, WHITE);
    prevY = yi;
  }
}

void drawWaveBand() {
  switch (currentMode) {
    case MODE_SCRUB:  drawScrubScan(); break;
    case MODE_FOLD:   drawFoldedWave(); break;
    case MODE_FILTER: drawFilterSweep(); break;
    case MODE_FM:     drawFmRipple(); break;
    default:          drawDecayingSineWave(); break; // tone, pluck, rickroll
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

  // --- incoming-data icon row (y 0-7, top-right, dedicated) ---
  // Skipped only during the rickroll flash, which already owns the whole
  // row for its own inverted-flash effect.
  if (!rickrolling) {
    drawStatusIconRow();
  }

  // --- waveform band (y WAVE_TOP..WAVE_TOP+WAVE_HEIGHT) — mode-specific,
  // see drawWaveBand() below for what each mode actually draws and why.
  drawWaveBand();

  // --- footer marquee (y 40-47), status icon badge masked over its left edge ---
  const char *footerText = rickrolling ? RICKROLL_FOOTER_TEXT : FOOTER_TEXT;
  display.setTextColor(WHITE);
  display.setCursor((int)footerX, FOOTER_Y);
  display.print(footerText);
  display.fillRect(W - ICON_ZONE_W - ICON_ZONE_PAD, FOOTER_Y, ICON_ZONE_W + ICON_ZONE_PAD, 8, BLACK); // opaque mask, badge + a little gap, so scrolling text never runs right up against it
  drawStatusHud();

  display.display();

  phase += PHASE_STEP;

  int footerWidth = strlen(footerText) * 6; // 6px/char at text size 1 (5px glyph + 1px spacing)
  footerX -= FOOTER_STEP;
  if (footerX < -footerWidth) {
    footerX = W;
  }
}

// Surfaces each boot phase on the OLED itself, not just Serial — added
// 2026-09-03 so a board that's stuck (WiFi out of range, relay
// unreachable) is diagnosable by looking at it, not just by whoever
// happens to have a laptop and a serial monitor plugged in at the venue.
// Reuses the info-line's y=0 slot and the waveform band's y=16 slot,
// both otherwise unused before playback starts — costs no screen space
// from the normal three-band layout above. Holds each message for
// BOOT_STATUS_HOLD_MS so it's actually readable, not just a flash.
void showBootStatus(const String &line1, const String &line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2.length() > 0) {
    display.setCursor(0, 16);
    display.println(line2);
  }
  display.display();
  delay(BOOT_STATUS_HOLD_MS);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[multi-synth-oled] starting");

  Wire.begin(D2, D1); // SDA, SCL
  // Fast-mode I2C (400kHz vs the 100kHz default) — see the
  // DIAG_DISABLE_OLED_DRAW comment at the top of this file: a full 384-byte
  // SSD1306 framebuffer write at 100kHz takes ~30ms+, long enough to stall
  // Mozzi's audio-rate timer for hundreds of samples and corrupt its
  // oscillator state, not just click. At 400kHz that same write drops to
  // roughly a quarter of that. Every SSD1306 module encountered in this
  // station's hardware supports Fast Mode; if a display ever doesn't ACK
  // after this, drop back to Wire.setClock(100000) as the first thing to try.
  Wire.setClock(400000);
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

  // Optional shields — unlike the OLED's own ACK check above, a missing or
  // failed BMP180 does NOT halt startup (bmp180_wifi.ino's own bare-board
  // sketch does halt, correctly, since sensing is that sketch's entire
  // job; here it's one optional feature on a synth board). dht.begin()
  // always succeeds regardless of whether anything's actually wired to
  // D5 yet — humidityPresent only flips true on a real successful read.
  Wire.beginTransmission(BMP180_I2C_ADDR);
  bmp180Present = (Wire.endTransmission() == 0) && bmp.begin();
  Serial.println(bmp180Present ? "[multi-synth-oled] BMP180 found" : "[multi-synth-oled] BMP180 not found (optional)");
  showBootStatus(bmp180Present ? "BMP180 OK" : "BMP180: none", "");
  dht.begin();

  showBootStatus("WiFi", WIFI_SSID);
  connectWiFi();
  showBootStatus("WiFi OK", WiFi.localIP().toString());

  showBootStatus("Relay", "fetching config...");
  fetchRelayConfig();
  showBootStatus("Relay cfg", relayHost + ":" + String(relayPort));

  connectDownlink();
  showBootStatus("Downlink", downlink.connected() ? "connected" : "failed, retrying");

  // Same startup-race fix as every Mozzi+ESP8266 sketch in this station —
  // see diagnostics/mozzi_startup_race_repro/.
  Serial.flush();
  delay(500);

  startMozzi();
  Serial.println("[multi-synth-oled] Mozzi started");
}

void updateControl() {
  pollDownlink();

  // Gated on a sensor already being known-present so a bare synth board
  // (no shields at all) never spends audio-timing budget on a DHT read
  // that would just time out. Known gap: a board with *only* a not-yet-
  // detected DHT22 and no BMP180 would never get tried at all, since
  // humidityPresent can only flip true from inside readAndPublishSensors()
  // itself — not a real limitation for this board, which always has the
  // BMP180 shield, but worth knowing if this sketch is ever reused on a
  // DHT-only variant.
  if ((bmp180Present || humidityPresent) && millis() - lastSensorReadAt >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadAt = millis();
    readAndPublishSensors();
  }

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

#if !DIAG_DISABLE_OLED_DRAW
  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawWaveform();
  }
#endif
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
