/*  Station 5 — Arduino UNO serial smoke test.

    No Mozzi, no buttons — just proves the serial link works before wiring
    anything else in. Prints one wire-format line every 3s in the exact
    format synth_relay.py's parse_line() expects, same shape
    uno_synth.ino's sendNoteEvent() will eventually emit on real button
    presses:

        note=60 velocity=100 deviceId=uno-direct-a synthType=smoketest

    Tried first over the (nonexistent, this board has no USB) Linksys
    bridge; this variant is for the direct-USB fallback instead — this
    board's Serial goes straight into whatever machine runs the relay, no
    OpenWrt/socat bridge in between. See station-5-synth/README.md for the
    fuller picture.
*/

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("note=0 velocity=0 deviceId=uno-direct-a synthType=smoketest-boot");
}

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 3000;

void loop() {
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    Serial.println("note=60 velocity=100 deviceId=uno-direct-a synthType=smoketest");
  }
}
