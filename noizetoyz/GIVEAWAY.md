# Giveaway kit wiring

Five take-home D1 Mini kits for IOSP 2026, built around the AliExpress cart (DIY-Victor
Store, ESP8266 D1 Mini Pro V3.0 line): **Dual Base**, **DC Power Shield**, **D1 Mini Micro
USB** (the ESP8266 board itself), **0.66" OLED shield**, **Dot Matrix LED shield** — ×5 each,
i.e. parts for 5 complete stacked kits, not 25 independent units. A DS18B20 (soldered
directly, no shield) and an SHT30 shield (on order, to experiment with) are being added on
top of that base kit. USB-powered — the DC Power Shield isn't being used (see the "Dropped"
note below).

This doc is wiring only. **No firmware for any of this yet** — see "Not yet done" at the
bottom before assuming any of this runs out of the box.

## Correction: the Dual Base's two columns share one net

Earlier draft of this doc assumed the Dual Base's second column was electrically dead
space — wrong. **Both columns are internally bridged, pin-for-pin, to the same D1 Mini.**
Populating only the left column with an actual D1 Mini still makes every corresponding pin
live on the right column's header — it's not a second independent socket, it's the same
signals broken out twice for physical convenience. Two real consequences:

- Anything soldered onto the right column's own through-holes (at the matching pin position)
  is on that net directly — **no jumper wires needed**, unlike the "run three wires across"
  version of this doc.
- A genuine shield (SHT30, Dot Matrix) can be **stacked directly** on the right column's
  header, same as stacking it on the left — it's driven by the one D1 Mini either way.

## Two variants being prototyped

**Variant A** — audio-focused:
- Left column: D1 Mini → OLED shield stacked on top.
- Right column: RC filter + DC-block + 3-pin screw terminal (see below) soldered directly
  onto the right column's own D4/GND/5V holes — no shield there, just discrete parts.

**Variant B** — sensor-focused:
- Left column: D1 Mini → OLED shield stacked on top.
- Right column: SHT30 shield → Dot Matrix LED shield stacked on top of it, both driven by
  the same left-column D1 Mini via the bridged base (real stacking, no wiring).

The DS18B20 (see below) fits either variant — solder it onto whichever column has D6/3V3/GND
still exposed (the top of a stack's pass-through header exposes every pin regardless of what
that particular shield actually uses).

## Pin budget

Everything below has to share one D1 Mini's GPIO pins, so the DS18B20 addition had to move
off its normal pin to avoid colliding with the Dot Matrix shield (Variant B) — kept off that
pin in Variant A too, so the same DS18B20 wiring works in both:

| Pin | Used by |
|---|---|
| D1, D2 | OLED shield, and SHT30 in Variant B (I²C — different addresses, same bus, same pattern already proven with BMP180+OLED elsewhere in this project) |
| D4 | Mozzi audio out → RC filter → amp terminal (Variant A) |
| D5, D7 | Dot Matrix LED shield, CLK/DIN (Variant B) |
| D6 | DS18B20 data (moved off its usual D5 — see below) |
| 3V3 | DS18B20 VDD + pull-up rail |
| 5V | Amp VDD (via terminal — not 3V3, same headroom reason as the existing LM386 wiring) |
| GND | shared by everything below |

A Buzzer Shield was considered for a giveaway sound indicator and dropped (2026-09-04): its
default control pin is also D5, colliding with the Dot Matrix shield, and it's a different
sound path entirely (a simple on/off or `tone()`-driven piezo, not Mozzi's real synthesis) —
not worth the added complexity for a giveaway unit.

The DC Power Shield (also in the cart) was dropped from these kits too (2026-09-04) — plain
USB power already proved sufficient for WiFi + Mozzi + the LM386 amp together in this
project's earlier real-hardware testing, so the wall-adapter option wasn't worth the extra
part/stack height for something people will carry around.

## Amp connector (RC lowpass + DC block) — Variant A

A clean 3-pin screw terminal replaces the bare wires this project's amp wiring has used up
to now (`D4` + `GND` into the LM386 module's top header, `5V` into its `VDD`). The filter
between D4 and the terminal is Mozzi's own recommended output filter, not an invented value —
270Ω/100nF gives a ~6kHz rolloff that clears the PWM carrier hiss, and the 10µF cap in series
afterward strips Mozzi's PWM DC bias so the signal swings around zero before reaching the
amp's `IN` pin, per [Mozzi's output circuits guide](https://sensorium.github.io/Mozzi/learn/output_circuits/).

Soldered straight onto the right column's own D4/GND/5V holes (see the correction above —
these are the same net as the left column's D1 Mini, no jumper wires needed):

```
                         ┌── R1 270Ω ──●── C2 10µF ── Terminal pin 1 (AUDIO → amp IN)
right-column D4 ─────────┘             │
                                     C1 100nF
                                        │
right-column GND ────────────────────GND rail────────── Terminal pin 2 (GND → amp GND)
right-column 5V  ──────────────────────────────────────── Terminal pin 3 (5V → amp VDD)
```

Parts per kit: R1 270Ω, C1 100nF (ceramic), C2 10µF (electrolytic is fine; two polarized caps
back-to-back work as a non-polarized substitute if that's easier to source), one 3-position
PCB-mount screw terminal block — 5.08mm pitch is the easiest to find/solder, but pitch doesn't
matter here since nothing needs to drop into an existing hole grid.

Mozzi's own docs also flag that ESP8266 GPIOs have a lower current rating than AVR and
recommend keeping the output circuit high-impedance — R1 already does that job, so nothing
downstream should draw current straight off D4 ahead of it.

## DS18B20 temperature sensor

Reuses this project's already-proven DS18B20 circuit (`D5 Sensor Wiring`, confirmed
2026-09-04 by swapping a DS18B20 onto the exact same wiring the DHT22 used) — **except moved
from D5 to D6**, since D5 is claimed by the Dot Matrix shield in Variant B. D6 (GPIO12) is a
plain general-purpose ESP8266 pin, no boot-strapping concerns, and already on this project's
own "safe on ESP8266" pin list (GPIO3/4/5/12/13/14) from the DHT22 library notes. Soldered
directly, not via a shield.

TO-92 package, flat face toward you, pins down, left to right: **GND, DQ (data), VDD**.

```
3V3 ──●──────────────────────────────── DS18B20 pin 3 (VDD)
      │
   10kΩ pull-up
      │
D6 ───●──────────────────────────────── DS18B20 pin 2 (DQ/DATA)
GND ──────────────────────────────────── DS18B20 pin 1 (GND)
```

Same 10kΩ pull-up value already verified working on this exact sensor/circuit shape — no new
value invented here.

## SHT30 shield (on order, Variant B)

I²C, same D1/D2 bus as the OLED shield — two user-selectable addresses, distinct from the
OLED's 0x3C, same non-conflicting-address pattern already proven with BMP180 (0x77) sharing
that bus elsewhere in this project. Stacks directly, no extra GPIO, no pull-up to solder —
gives temperature *and* humidity in one shield, which is what this project wanted all along
before the DHT22 units turned out dead. Not yet in hand — ordered to experiment with, not yet
confirmed on real hardware in this kit.

## Not yet done

- **No firmware for any of this.** The dot-matrix shield has no driver anywhere in
  `noizetoyz/firmware/` yet, and neither does the SHT30. The DS18B20 addition needs a
  one-line pin change wherever it's read (`ds18b20_test.ino` and any sketch built from it
  currently hardcode D5, not D6) — not done, on hold per Torsten's instruction until the
  wiring itself is confirmed on real hardware.
- **Neither variant has been built or tested yet.** This is a wiring spec derived from proven
  values elsewhere in this project (Mozzi's own filter recommendation, this project's own
  DS18B20/DHT22 circuit, Wemos's own shield pinouts) plus the Dual Base's bridging behavior
  as confirmed by Torsten — not a from-scratch guess, but nothing here has been soldered yet.
- Which variant (or both) becomes the actual giveaway build, and whether all 5 kits match or
  some get one variant and some the other, isn't decided — see `tracker-vss7`'s 2026-09-04
  hardware entries for the open planning thread.
