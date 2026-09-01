/*  Station 5 — scrolling waveform animation for the Wemos OLED Shield
    (64x48 SSD1306, I2C) stacked on the D1 mini.

    Standalone visual — no WiFi, no Mozzi, no relay — just proves the OLED
    itself and its exact library/pin/address requirements, which are
    non-obvious for this specific shield (a plain SSD1306 constructor with
    128x64/128x32 assumptions won't work: the physical panel is 64x48, and
    the mainline Adafruit_SSD1306 library needs a library-level macro set
    before compiling, not a runtime width/height argument, to know that).

    Library: "Adafruit SSD1306 Wemos Mini OLED" (Adafruit + mcauser, via
    Stefan Bethke's fork) — chosen over plain Adafruit_SSD1306 because it's
    purpose-built for this exact 64x48 Wemos shield and ships a
    ssd1306_64x48_i2c example this sketch's init sequence is copied from
    directly (constructor, begin() call, I2C address, reset-pin handling)
    rather than guessed.

    Pins (fixed by the shield's stacking header, confirmed via that
    example's own comments and web search): SCL=D1 (GPIO5), SDA=D2 (GPIO4).
    I2C address 0x3C. No reset line is actually broken out on the shield's
    header — GPIO0 is passed as OLED_RESET only because the library's own
    example does the same for this exact board; toggling it post-boot
    (ESP8266's boot-mode strapping only matters at power-on) is harmless.

    UNTESTED as of writing — about to be flashed to the real D1 mini+OLED
    stack this session used for the WiFi smoke test.
*/

#define SSD1306_64_48
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET 0
Adafruit_SSD1306 display(OLED_RESET);

const int W = 64;
const int H = 48;
const int MID = H / 2;

float phase = 0.0;
const float PHASE_STEP = 0.25;   // how fast the wave scrolls
const float FREQ = 0.15;         // cycles per pixel-column of the primary wave
const float AMPLITUDE = 14.0;    // primary wave amplitude, pixels
const float FREQ2 = 0.37;        // a second, faster/quieter harmonic for texture
const float AMPLITUDE2 = 5.0;

void setup() {
  Wire.begin(D2, D1); // SDA, SCL
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
}

void loop() {
  display.clearDisplay();

  int prevY = -1;
  for (int x = 0; x < W; x++) {
    float y = MID
      + AMPLITUDE * sin(2.0 * PI * FREQ * x + phase)
      + AMPLITUDE2 * sin(2.0 * PI * FREQ2 * x - phase * 1.7);
    int yi = (int)y;
    if (yi < 0) yi = 0;
    if (yi >= H) yi = H - 1;

    if (prevY >= 0) {
      display.drawLine(x - 1, prevY, x, yi, WHITE);
    } else {
      display.drawPixel(x, yi, WHITE);
    }
    prevY = yi;
  }

  display.display();

  phase += PHASE_STEP;
  if (phase > 2.0 * PI * 1000) phase -= 2.0 * PI * 1000; // keep it bounded, harmless either way

  delay(30); // ~33fps target; actual rate limited by I2C buffer flush time
}
