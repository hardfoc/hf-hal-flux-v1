# LedManager

Drives the single WS2812 RGB status LED: a colour, a brightness, and a few status animations (boot, ready, warning,
error, calibrating). Source: [`LedManager.h`](../../lib/managers/LedManager.h).

## API

| Call | Notes |
| --- | --- |
| `SetColor(const LedColor&)`, `SetColor(uint32_t rgb)` | Solid colour; `LedColors::GREEN`, `LedColors::RED`, ... are predefined |
| `TurnOff()` | |
| `SetBrightnessPercent(0-100)`, `SetBrightnessRaw(0-255)`, `SetMaxBrightness(0-255)` | `SetMaxBrightness` caps what 100 % means |
| `StartAnimation(LedAnimation, color)`, `StopAnimation()`, `IsAnimationActive()` | `OFF`, `SOLID`, `BLINK`, `BREATH`, `RAINBOW` and the status animations |
| `UpdateAnimation()` | Advances the running animation; **call it periodically** |
| `IndicateBoot()`, `IndicateReady()`, `IndicateWarning()`, `IndicateError()`, `IndicateCalibration()` | Status animations in blue, green, yellow, red and purple |
| `GetCurrentColor`, `GetCurrentBrightnessPercent`, `GetCurrentGpioPin`, `GetLedCount` | Read back |
| `GetLastError()`, `GetSystemDiagnostics(LedSystemDiagnostics&)`, `DumpStatistics()` | |

## Example: show status from a task

```cpp
#include "api/Flux.h"
#include "managers/LedManager.h"

auto& leds = Flux::GetInstance().leds;
(void)leds.SetBrightnessPercent(30);
(void)leds.IndicateReady();

// In a periodic task (every 20-50 ms):
(void)leds.UpdateAnimation();
```

## Diagnostics

`GetSystemDiagnostics()` reports whether the LED is up, the current animation, brightness and colour, operation
counters and the number of animation steps run.

## Pitfalls

- **Animations do not run by themselves.** The HAL creates no task; nothing changes until you call
  `UpdateAnimation()`. It steps at most every 50 ms, so calling it faster is harmless.
- **One LED.** `led_index` arguments other than 0 have nothing to address (`GetLedCount()` is 1).
- **Pin** comes from `WS2812_LED_DAT` in the pin map (GPIO 3 is the fallback if the mapping is missing).
