/*  Quick standalone test — DS18B20 on the same breadboard/pull-up/D5
    wiring the DHT22 test used (see dht22_wifi.ino's header for that
    wiring's full history). No WiFi, no relay — just proving the sensor
    itself reads, isolated from everything else, the same way the DHT22
    NaN was isolated to the sensor rather than the WiFi/relay path.

    DS18B20 pinout (bare TO-92, flat face toward you, pins down):
    1 GND, 2 DQ (data), 3 VDD. Same 3.3V-not-5V reasoning as the DHT22 —
    keep DQ's HIGH level matched to the ESP8266's logic level. Datasheet
    pull-up spec is 4.7k between VDD/DQ; the existing 10k from the DHT22
    wiring is left as-is here rather than rewired, since 10k is commonly
    used in practice and should still work at breadboard wire lengths.

    Not yet run on real hardware — first flash pending.
*/

#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_PIN D5  // same physical pin the DHT22 test used

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[ds18b20-test] starting");

  sensors.begin();
  int count = sensors.getDeviceCount();
  Serial.print("[ds18b20-test] devices found on the bus: ");
  Serial.println(count);
  if (count == 0) {
    Serial.println("[ds18b20-test] no DS18B20 detected — check DQ/pull-up/power wiring");
  }
}

void loop() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("[ds18b20-test] read failed — sensor not responding");
  } else {
    Serial.print("[ds18b20-test] temp=");
    Serial.print(tempC);
    Serial.println("C");
  }
  delay(2000);
}
