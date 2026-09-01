/*  Station 5/2 crossover — BMP180 smoke test (Wemos D1 mini + BMP180 Shield).

    No WiFi, no ATProto — just proves the sensor itself reads on real
    hardware before wiring it into any network path. This is the second
    Wemos stack from the hardware pile (see station-5-synth/README.md's
    2026-09-01 hardware-inventory note) — a real candidate for station-2's
    temp/(pressure, not humidity) sensor gadget, sourced over WiFi instead
    of local GPIO.

    BMP180 gives temperature + barometric pressure, not humidity — station-2
    lexicon's `sensorType`/`unit` fields would need "pressure"/"pascal" (or
    similar) alongside "temperature"/"celsius", not "humidity"/"percent".

    Pins: same I2C convention as the other Wemos shield stack (OLED) —
    SCL=D1 (GPIO5), SDA=D2 (GPIO4). BMP180's I2C address is fixed (0x77),
    no address pin to configure.

    REAL-HARDWARE FINDING 2026-09-01: the first version of this sketch
    called bmp.begin() directly and hung completely — no Serial output at
    all, confirmed (via a separate trivial no-I2C sketch, same board) not a
    UART/upload problem. Root cause: the classic ESP8266 Arduino core's
    Wire library has no timeout at all by default; a stuck/floating I2C bus
    (shield not fully seated on the stacking header is the likely cause
    here) blocks forever inside bmp.begin()'s I2C read, before it ever gets
    a chance to print "not found". Fixed two ways: Wire.setClockStretchLimit()
    bounds the classic ESP8266 hang-on-stretched-clock failure mode, and a
    manual, already-bounded beginTransmission()/endTransmission() presence
    check runs *before* the (still-blocking) library begin() call, so a
    genuinely absent/disconnected sensor now prints a clear error instead
    of hanging silently.
*/

#include <Wire.h>
#include <Adafruit_BMP085.h>

#define BMP180_I2C_ADDR 0x77

Adafruit_BMP085 bmp;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[bmp180-smoketest] starting");

  Wire.begin(D2, D1); // SDA, SCL
  Wire.setClockStretchLimit(1000); // microseconds — bounds the classic ESP8266 hang-on-stuck-SCL failure mode

  Wire.beginTransmission(BMP180_I2C_ADDR);
  uint8_t i2cResult = Wire.endTransmission();
  if (i2cResult != 0) {
    while (1) {
      Serial.print("[bmp180-smoketest] no ACK from 0x77 (endTransmission=");
      Serial.print(i2cResult);
      Serial.println(") — check the shield is fully seated on the stacking header");
      delay(1000);
    }
  }

  if (!bmp.begin()) {
    while (1) {
      Serial.println("[bmp180-smoketest] I2C ACK OK but BMP180 library begin() failed — check sensor identity/wiring");
      delay(1000);
    }
  }
  Serial.println("[bmp180-smoketest] BMP180 found, reading every 2s");
}

void loop() {
  float tempC = bmp.readTemperature();
  int32_t pressurePa = bmp.readPressure();

  Serial.print("[bmp180-smoketest] temp=");
  Serial.print(tempC);
  Serial.print("C pressure=");
  Serial.print(pressurePa);
  Serial.println("Pa");

  delay(2000);
}
