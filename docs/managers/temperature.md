# TemperatureManager

Reads the board's three temperature sensors by name and keeps simple statistics. Source:
[`TemperatureManager.h`](../../lib/managers/TemperatureManager.h).

| Name | Sensor | Read through |
| --- | --- | --- |
| `ESP32_INTERNAL` | The ESP32-S3's internal temperature sensor | `EspTemperature` |
| `NTC_THERMISTOR` | 10 kΩ NTC, B = 3950, 10 kΩ series resistor, 3.3 V | `AdcManager` channel `ESP32_NTC_THERMISTOR` |
| `MCP9700_THERMISTOR` | MCP9700 analog sensor | `AdcManager` channel `ADS7952_0_CH8` (needs `HARDFOC_MCP9700_SUPPORT` and a working ADS7952) |

The manager starts if at least one sensor works; the others are left out and reads of them return
`SENSOR_NOT_FOUND`.

## API

| Call | Notes |
| --- | --- |
| `ReadTemperatureCelsius(name, float&)`, `ReadTemperatureFahrenheit(name, float&)` | By name |
| `ReadInternalTemperature(float&)`, `ReadNtcTemperature(float&)`, `ReadMcp9700Temperature(float&)` | Shortcuts for the three names |
| `GetSystemTemperatureStats(min, max, avg)` | Reads every registered sensor and summarises |
| `GetSensorCount()`, `GetSensorEntry(name)` | Registered sensors; `TempEntry` keeps the last reading and counters |
| `GetLastError()`, `GetSystemDiagnostics(TempSystemDiagnostics&)`, `DumpStatistics()` | |

## Example: board temperature with a fallback

```cpp
#include "api/Flux.h"
#include "managers/TemperatureManager.h"

auto& temp = Flux::GetInstance().temp;
float c = 0.0f;
if (temp.ReadNtcTemperature(c) != TemperatureError::SUCCESS) {
    (void)temp.ReadInternalTemperature(c);  // the die sensor runs warmer than the board
}
```

## Diagnostics

`GetSystemDiagnostics()` gives registered sensors, operation counters, and the minimum, maximum and average of the
last readings. `DumpStatistics()` logs each sensor's last value and counters.

## Pitfalls

- **Start-up order matters.** The thermistors depend on `AdcManager`; if the ADC failed, only `ESP32_INTERNAL`
  registers.
- **The internal sensor measures the chip**, not the valves or the board; it reads higher than ambient.
- **NTC values are fixed in code** (B, series resistor, reference voltage). A different thermistor means changing
  `TemperatureManager.cpp`.
