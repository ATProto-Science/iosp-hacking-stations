# Webcam hue calibration notes

Raw data for rebuilding `WEBCAM_EMOJI.hue`'s bucket boundaries in
`toons/index.html` and `landing-page/viewer.html` — currently still the
disproven standard-HSV-shaped assumption (red≈0°, yellow≈60°, etc.),
confirmed wrong 2026-09-06 by a synthetic-color ffmpeg test (see
`calib.py`'s own docstring) and now by real cards below. **Not yet
applied to production code** — waiting on a second pass under daylight
before touching the actual bucket table (see below).

Measured with `calib.py` (8-sample averages, circular mean for hue) via
`/dev/video0`, this machine's built-in camera.

## Session 1 — 2026-09-06 night, warm/tungsten lamp only

| card | HUEAVG | SATAVG | spread | confidence |
|---|---|---|---|---|
| green | 101° | 24 | 4° | ok |
| yellow | 118° | 52 | 9° | good |
| orange | 154° | 69 | 0.2° | good |
| blue | 156° | 5 | 1.8° | **unreliable** — see below |
| red | 176° | 70 | 4° | good |

**Blue is not trustworthy from this session.** A warm/tungsten light
source emits very little blue light, so a blue card has little blue
light to reflect back — it measured essentially desaturated (SATAVG=5,
same territory as a synthetic all-white/all-black frame, where hue is
physically meaningless). Its 156° reading landing in the middle of the
warm-color cluster is very likely just noise, not a real signal — not
"blue actually looks like orange," but "there was nothing real to
measure." **Repeated once more the same night**: SATAVG=5.03, near-
identical to the first attempt's 5.06 — confirms this is a reproducible
low-saturation floor, not a one-off glitch (hue itself drifted, 174° vs
156°, exactly as expected when saturation this low makes the hue angle
numerically unstable). Physical lighting limitation, not a framing or
camera issue.

**What tonight's good (red/orange/yellow/green) data does show**: a
clean, monotonically increasing, evenly-spaced progression exactly
matching real color-wheel adjacency — green (101°) → yellow (118°) →
orange (154°) → red (176°) — just compressed into a ~75° span rather
than the ~180° a naive standard-HSV assumption would predict. That
compression itself may partly be a warm-lighting artifact too (worth
seeing if it's still this tight under daylight).

## Still needed

- A daylight/neutral-light recalibration pass (planned for
  2026-09-07) — specifically to get a trustworthy blue reading, and
  ideally cyan/purple too so the full wheel is covered, not just the
  warm half.
- Only after that: rewrite `WEBCAM_EMOJI.hue`'s bucket boundaries in
  both `toons/index.html` and `landing-page/viewer.html` from the
  combined dataset, and decide what to do below some saturation
  threshold (tonight's blue result suggests low-saturation readings
  should probably fall back to a neutral/grey indicator instead of
  guessing a hue-based color at all — a bucket table can't fix a
  physical lack of color signal).

## Method notes

- `calib.py` grabs 8 samples/cycle by default (`--samples`), averaged
  (hue via circular mean, since it wraps at 360°).
- Each reading above was preceded by one **discarded warm-up frame**
  (`grab_one()` called once and thrown away before the real
  `grab_averaged()` call) — a cold-opened camera's auto-exposure/
  auto-white-balance hasn't converged on the very first frame, and an
  early debug capture without a warm-up frame came back as a flat,
  near-black, near-zero-saturation frame even in good light. Worth
  considering baking a warm-up discard into `webcam-grab.sh`/
  `webcam_producer.py` itself, since every production reading is also
  a cold open of the device (see `OPS.md`).
- Card should fill most of the frame (confirmed visually this session
  — framing was not the issue) but a few cm of margin is fine; distance
  guidance is in the worksheet/conversation, roughly 20-30cm for a
  typical laptop webcam.
