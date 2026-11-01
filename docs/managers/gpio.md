# GpioManager

Creates one ESP32 GPIO object for every *GPIO*-category pin of the pin map and lets you drive or read it by name or
by enum. On Flux V1 those are the control and fault lines of the two valve drivers. Source:
[`GpioManager.h`](../../lib/managers/GpioManager.h).

## Which pins it owns

At start-up it walks the pin map and registers each pin whose category is GPIO (or user) and whose chip is the
ESP32. Core pins (boot, USB) and comm pins (SPI, CAN, UART, LED data) are skipped; their owners configure them. The
names are the `Name` column of the [pin map](../hardware/pin-map.md): `GPIO_MAX22200_ENABLE`, `GPIO_MAX22200_CMD`,
`GPIO_MAX22200_FAULT_N`, `GPIO_MAX22200_TRIGA`, `GPIO_MAX22200_TRIGB`, `GPIO_TLE92466_EN`, `GPIO_TLE92466_RESN`,
`GPIO_TLE92466_DRV0`, `GPIO_TLE92466_DRV1`, `GPIO_TLE92466_FAULT_N`.

Each pin gets its polarity, pull and drive from the pin map. "Active" means the pin's asserted level, so for an
active-low pin `SetActive()` drives it low.

## API

| Call | Notes |
| --- | --- |
| `Get(std::string_view name)`, `Get(HfFunctionalGpioPin pin)` | `BaseGpio*`, or `nullptr` if not registered |
| `Set(name, bool active)`, `SetActive(name)`, `SetInactive(name)`, `Toggle(name)` | Return `hf_gpio_err_t` |
| `Read(name, bool& active)`, `IsActive(name, bool& active)` | Logical state (polarity applied) |
| `SetDirection`, `SetPullMode`, `SetOutputMode` | Reconfigure a pin at run time |
| `ConfigureInterrupt(name, trigger, callback, user_data)`, `EnableInterrupt`, `DisableInterrupt`, `SupportsInterrupts` | Edge interrupts on a registered pin |
| `Contains(name)`, `Size()` | Lookup helpers |
| `GetLastError()`, `GetSystemDiagnostics(GpioSystemDiagnostics&)`, `DumpStatistics()` | Operation counters and the last error |

## Example: watch a fault line

```cpp
#include "api/Flux.h"
#include "managers/GpioManager.h"

auto& gpio = Flux::GetInstance().gpio;
bool fault = false;
if (gpio.Read("GPIO_TLE92466_FAULT_N", fault) == hf_gpio_err_t::GPIO_SUCCESS && fault) {
    // the TLE92466ED is asserting its fault output (pin is active low; Read() returns "active")
}
```

Prefer the enum form (`gpio.Get(HfFunctionalGpioPin::TLE92466_FAULT_N)`) in loops: the string form searches the
table on every call.

## Diagnostics

`GetSystemDiagnostics()` reports the number of registered pins and the total, successful and failed operation
counts. `DumpStatistics()` logs the same and the per-pin access and error counts.

## Pitfalls

- **Every pin starts as an output.** Registration configures all pins as outputs, including the `_FAULT_N` inputs.
  `ValveManager` switches the two fault pins to inputs during its own start-up; if you use a pin as an input before
  that, call `SetDirection(name, HF_GPIO_DIRECTION_INPUT)` first.
- **The valve drivers own their control pins.** Changing `GPIO_TLE92466_EN` or `GPIO_MAX22200_ENABLE` behind
  `ValveManager`'s back disables outputs it thinks are on.
- **No lock on pin operations.** See [Threading](../architecture/threading.md).
- **A wrong name** returns `GPIO_ERR_PIN_NOT_FOUND`, not a crash. Copy names from the pin map.
