/*  Noizetoyz — minimal repro of a real ESP8266+Mozzi startup-timing bug,
    found and fixed 2026-09-01 while esp_note_player.ino appeared to hang
    completely silently (zero serial output, on both esp8266 core 3.1.2
    and 3.0.2 — ruled out as a core-version regression by testing both).

    Bisected down to this: a global Oscil + Mozzi.h + startMozzi(), with
    NO OLED, NO WiFi, nothing else. Any Serial.println() called
    immediately before startMozzi() (no flush/delay) never arrived —
    startMozzi()'s timer/interrupt setup briefly disrupts UART
    transmission right as it runs, and bytes still sitting in the FIFO
    at that exact moment can be lost. Not a crash: loop()/audioHook()
    keep running fine afterward, proven by uncommenting the "confirms
    it's alive" block below and watching it print reliably forever —
    only the pre-startMozzi() messages were ever actually at risk.

    Fix, applied here and in every real Mozzi+ESP8266 sketch in this
    station since: Serial.flush() + a short delay (500ms was plenty)
    right before calling startMozzi(). Cheap insurance, no downside to
    including it even where it turns out not to matter.
*/

#define MOZZI_CONTROL_RATE 128
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> aSin(SIN2048_DATA);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[repro] before startMozzi()");

  // THE FIX — comment these two lines out to reproduce the original bug
  // (the line above will then reliably fail to arrive).
  Serial.flush();
  delay(500);

  aSin.setFreq(440);
  startMozzi();
  Serial.println("[repro] after startMozzi()");
}

void updateControl() {
}

AudioOutput updateAudio() {
  return MonoOutput::from8Bit(aSin.next());
}

void loop() {
  // confirms it's alive: even when the two lines above are missing and
  // the earlier prints are lost, this keeps printing reliably — proof
  // the sketch was never actually crashed, just racing the UART FIFO.
  static unsigned long last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.println("[repro] loop alive");
  }
  audioHook();
}
