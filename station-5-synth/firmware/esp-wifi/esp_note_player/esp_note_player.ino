/*  Station 5 — use case 1: browser piano keyboard -> this Wemos plays it.

    webapp/index.html -> synth_relay.py (HTTP POST /note -> real ATProto
    write, proven working 2026-09-01) -> its Jetstream downlink ->
    BROADCAST_PORT -> this sketch, connected as a plain downlink client
    (same wire-line protocol and connect/parse pattern as
    sdiy/mozzi-noizetoyz's timonsfiretruck-esp32, but playing the note
    itself via Mozzi instead of steering a siren).

    Every received note-on:
      - plays an audible tone via Mozzi (sine oscillator, ~400ms), and
      - drives the OLED Shield's scrolling waveform (ported from
        ../oled_waveform/oled_waveform.ino) so its visual density/speed
        changes with the note received — a live visual indicator of
        incoming notes, not just audio.

    Runs on the Wemos D1 mini + OLED Shield stack — same board
    oled_waveform.ino was proven on. Audio out D4/GPIO2 (Mozzi, fixed),
    OLED on D1/D2 I2C (SCL/SDA) — no pin conflict, confirmed when
    oled_waveform.ino was first written.

    The OLED redraw is deliberately throttled (~20fps) inside
    updateControl() rather than drawn every control tick or via delay() —
    Mozzi owns loop() through audioHook(), so a blocking delay() or a
    full 384-byte I2C redraw on every control tick (128/s) would stall
    audio timing; gating it to roughly every 5th tick keeps the animation
    smooth without touching I2C anywhere near that often.

    UNTESTED — no board free to flash against while writing this (both
    Wemos stacks were mid-use for other tests this session); built directly
    from oled_waveform.ino's and esp_synth.ino's already-verified pieces,
    not independently confirmed as a combination.
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define MOZZI_CONTROL_RATE 128
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

#define SSD1306_64_48
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- fill in for your workshop WiFi ----
const char *WIFI_SSID = "noizetoyz";
const char *WIFI_PASSWORD = "synthbeep";
// ------------------------------------------------------

// Relay location is discovered via ATProto at boot (see fetchRelayConfig()
// below) instead of hardcoded — same mechanism as bmp180_wifi.ino, added
// after this exact IP changed under us three separate times in one
// session. DEFAULT_RELAY_HOST is only the fallback if that fetch fails —
// which, as of this writing, it always will: music.atproto.noizetoyz.synth.relayConfig
// isn't registered with HappyView yet (same blocker as .note/.listNotes,
// still pending the admin key). Worth having the real mechanism in place
// regardless — it starts working the moment registration happens, no
// firmware change needed.
const char *DEFAULT_RELAY_HOST = "192.168.1.20"; // laptop's wired LAN IP (WiFi stays on the home network instead)
const uint16_t DEFAULT_RELAY_PORT = 8479;        // synth_relay.py's SYNTH_BROADCAST_PORT (downlink)

const char *HAPPYVIEW_URL = "https://happyview.werk.museum";
const char *HAPPYVIEW_CLIENT_KEY = "hvc_4f63d844f5a253fe658f1491160126dc";

String relayHost;
uint16_t relayPort;

Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSin(SIN2048_DATA);

#define OLED_RESET 0
Adafruit_SSD1306 display(OLED_RESET);

WiFiClient downlink;
String lineBuffer;

int lastNote = 60;
bool sounding = false;
unsigned long noteOffAt = 0;

// Screen layout — 64x48 total, split into three bands since the note
// info line and footer marquee both needed to be added without crowding
// the waveform out entirely:
//   y  0- 7: note name + duration (text size 1, 8px tall)
//   y 10-37: waveform, in its own 28px band (not the full screen height
//            anymore — MID/AMPLITUDE below are relative to this band)
//   y 40-47: scrolling footer marquee (text size 1, 8px tall)
const int W = 64;
const int WAVE_TOP = 10;
const int WAVE_HEIGHT = 28;
const int WAVE_MID = WAVE_TOP + WAVE_HEIGHT / 2;
const int NOTE_DURATION_MS = 400; // matches noteOffAt's fixed play length below

float phase = 0.0;
const float PHASE_STEP = 0.25;
// Amplitude decays from PEAK down to IDLE after each note, exponentially,
// instead of staying constant — reads as the wave "settling" after being
// struck, like a plucked-string envelope, rather than a flat oscilloscope
// trace. IDLE stays nonzero so the screen never goes fully still.
const float PEAK_AMPLITUDE = 14.0;
const float IDLE_AMPLITUDE = 2.0;
const float DECAY_TAU_MS = 600.0; // time constant — bigger = slower decay
unsigned long noteStartAt = 0;
unsigned long lastDraw = 0;
const unsigned long DRAW_INTERVAL_MS = 50; // ~20fps — see header comment on why this is throttled

const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

const char *FOOTER_TEXT = "ATScience|ATMusic|IOSP Hacking Station";
float footerX = 64; // starts off the right edge, scrolls left
const float FOOTER_STEP = 0.6; // slower than the waveform's own phase — text needs to stay readable

float midiToFreq(int note) {
  return 440.0 * pow(2.0, (note - 69) / 12.0);
}

// Maps the received MIDI note to the waveform's visual cycles-per-pixel —
// higher notes draw a denser/faster-looking wave, lower notes a wider one,
// so the animation's character actually tracks what's being played.
float noteToVisualFreq(int note) {
  return 0.05 + (constrain(note, 36, 84) - 36) / 48.0 * 0.35;
}

// "C4" style name — MIDI note 60 is C4 by the standard convention this
// follows (octave = note/12 - 1).
String midiNoteName(int note) {
  int octave = note / 12 - 1;
  return String(NOTE_NAMES[note % 12]) + String(octave);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[note-player] connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[note-player] WiFi up, IP=");
  Serial.println(WiFi.localIP());
}

// Fetches the most recent music.atproto.noizetoyz.synth.relayConfig record and sets
// relayHost/relayPort from it — see bmp180_wifi.ino's identical function
// for the full reasoning. Falls back to DEFAULT_RELAY_HOST/PORT on any
// failure (network error, bad JSON, no records, lexicon not registered
// yet — see this file's header comment) and never blocks/crashes startup.
void fetchRelayConfig() {
  relayHost = DEFAULT_RELAY_HOST;
  relayPort = DEFAULT_RELAY_PORT;

  WiFiClientSecure httpsClient;
  httpsClient.setInsecure(); // no cert store on this MCU — same tradeoff every ESP8266 HTTPS sketch makes
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
  Serial.print("[note-player] fetching relay config: ");
  Serial.println(url);

  if (!http.begin(httpsClient, url)) {
    Serial.println("[note-player] relayConfig fetch: begin() failed, using default");
    return;
  }
  http.addHeader("X-Client-Key", HAPPYVIEW_CLIENT_KEY);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[note-player] relayConfig fetch: HTTP ");
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
    Serial.print("[note-player] relayConfig fetch: JSON parse failed (");
    Serial.print(err.c_str());
    Serial.println("), using default");
    return;
  }

  // HappyView doesn't guarantee list order — client-side sort by
  // createdAt via plain string comparison, same as bmp180_wifi.ino
  // (ISO8601 UTC timestamps sort lexicographically in chronological
  // order too).
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
    Serial.print("[note-player] relay config from ATProto: ");
    Serial.print(relayHost);
    Serial.print(":");
    Serial.println(relayPort);
  } else {
    Serial.println("[note-player] relayConfig fetch: no records found, using default");
  }
}

void connectDownlink() {
  Serial.println("[note-player] connecting to relay downlink...");
  if (downlink.connect(relayHost.c_str(), relayPort)) {
    Serial.println("[note-player] downlink connected");
  } else {
    Serial.println("[note-player] downlink connect failed, will retry");
  }
}

// Parses one wire line ("note=60 velocity=100 ..."), same format as every
// other device in this station — see synth_relay.py's record_to_line().
void applyLine(const String &line) {
  int note = -1;

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
    }
    start = space + 1;
  }

  if (note >= 0) {
    lastNote = note;
    aSin.setFreq(midiToFreq(note));
    sounding = true;
    noteOffAt = millis() + NOTE_DURATION_MS;
    noteStartAt = millis(); // drives the waveform's decay envelope, independent of audio duration
    Serial.print("[note-player] playing note=");
    Serial.println(note);
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

  // --- note info line (y 0-7) ---
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print(midiNoteName(lastNote));
  display.print(" ");
  display.print(NOTE_DURATION_MS);
  display.print("ms");

  // --- waveform, decaying envelope (y WAVE_TOP..WAVE_TOP+WAVE_HEIGHT) ---
  float elapsed = millis() - noteStartAt;
  float amplitude = IDLE_AMPLITUDE + (PEAK_AMPLITUDE - IDLE_AMPLITUDE) * exp(-elapsed / DECAY_TAU_MS);
  float freq = noteToVisualFreq(lastNote);

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
  display.setCursor((int)footerX, 40);
  display.print(FOOTER_TEXT);

  display.display();

  phase += PHASE_STEP;

  int footerWidth = strlen(FOOTER_TEXT) * 6; // 6px/char at text size 1 (5px glyph + 1px spacing)
  footerX -= FOOTER_STEP;
  if (footerX < -footerWidth) {
    footerX = W;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[note-player] starting");

  Wire.begin(D2, D1); // SDA, SCL
  Wire.setClockStretchLimit(1000); // microseconds — see bmp180_smoke_test.ino's REAL-HARDWARE FINDING note; the classic ESP8266 Wire library has no timeout by default and a stuck/miswired I2C bus hangs forever without this

  Wire.beginTransmission(0x3C);
  uint8_t oledResult = Wire.endTransmission();
  if (oledResult != 0) {
    while (1) {
      Serial.print("[note-player] no ACK from OLED at 0x3C (endTransmission=");
      Serial.print(oledResult);
      Serial.println(") — check the OLED shield is fully seated on the stacking header");
      delay(1000);
    }
  }

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  connectWiFi();
  fetchRelayConfig();
  connectDownlink();

  // REAL-HARDWARE FINDING 2026-09-01: startMozzi() briefly disrupts UART
  // transmission right as it sets up its timer/interrupt — any
  // Serial output queued but not yet physically drained over the wire at
  // that exact moment can be lost (confirmed via a minimal isolation
  // sketch: identical prints right before startMozzi() never arrived
  // without this flush+delay, but the sketch was actually running fine
  // the whole time — audioHook()'s own loop kept executing correctly).
  // Not a crash, just a startup race; this is the fix.
  Serial.flush();
  delay(500);

  startMozzi();
  aSin.setFreq(0);
  Serial.println("[note-player] Mozzi started");
}

void updateControl() {
  pollDownlink();

  if (sounding && millis() > noteOffAt) {
    aSin.setFreq(0);
    sounding = false;
  }

  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawWaveform();
  }
}

AudioOutput updateAudio() {
  return MonoOutput::from8Bit(aSin.next());
}

void loop() {
  audioHook();
}
