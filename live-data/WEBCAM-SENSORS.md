# Webcam as a sensor

`./webcam-grab.sh [reading]` grabs one frame and prints a numeric reading
from it, no Python image library needed — just `ffmpeg`'s own `signalstats`
filter. Four readings available, all verified working on Linux
(`AVFOUNDATION=1 ./webcam-grab.sh [reading]` for macOS):

| reading | what it is |
|---|---|
| `brightness` (default) | average luma — how bright the frame is |
| `saturation` | average colorfulness — grey vs. vivid |
| `hue` | average hue angle — dominant color |
| `contrast` | luma spread (max−min) — flat vs. high-contrast |

Mic as a noise sensor works the same way in spirit (`sounddevice` or
`ffmpeg -f alsa` + `astats` instead of `signalstats`), just not scripted
here yet.

## Wiring it into `sensor_producer.py`

`webcam_sensors.py` exposes each reading as a plain function
(`read_brightness()`, `read_saturation()`, `read_hue()`, `read_contrast()`)
— no need to shell out yourself. Two call-site changes:

```python
import webcam_sensors

UNIT = {
    "temperature": "celsius",
    "brightness": "luma",
    "saturation": "chroma",
    "hue": "degrees",
    "contrast": "luma",
}.get(SENSOR_TYPE, "percent")

def read_sensor():
    if SENSOR_TYPE == "brightness":
        return round(webcam_sensors.read_brightness(), 1)
    if SENSOR_TYPE == "saturation":
        return round(webcam_sensors.read_saturation(), 1)
    if SENSOR_TYPE == "hue":
        return round(webcam_sensors.read_hue(), 1)
    if SENSOR_TYPE == "contrast":
        return round(webcam_sensors.read_contrast(), 1)
    if SENSOR_TYPE == "temperature":
        return round(random.uniform(18.0, 24.0), 1)
    return round(random.uniform(30.0, 60.0), 1)
```

Then run with `SENSOR_TYPE=brightness ./run_producer.sh` instead of the
default. `UNIT` needed its own mapping since the original `"celsius" if
temperature else "percent"` only covered two cases.
