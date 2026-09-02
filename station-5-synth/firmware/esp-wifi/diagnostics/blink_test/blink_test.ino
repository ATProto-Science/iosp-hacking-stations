/*  Station 5 — trivial "does the board/USB/toolchain even work" diagnostic.

    No WiFi, no Mozzi, no I2C — just proves the serial link and upload
    pipeline are healthy before suspecting anything sketch-specific. Used
    repeatedly this session to isolate real bugs (a WiFiClient truncation
    race, a silent OLED I2C hang, a Mozzi startup-timing race — see
    ../mozzi_startup_race_repro/) from "did the board/cable/capture just
    stop working." If this sketch doesn't reliably print, the problem is
    hardware/USB/toolchain, not application code — check that first.
*/

void setup() {
  Serial.begin(115200);
  delay(200);
}

void loop() {
  Serial.println("[blinktest] alive");
  delay(500);
}
