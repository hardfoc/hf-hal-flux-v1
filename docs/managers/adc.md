# AdcManager

Every analog input of the board behind one interface: the two ESP32-S3 ADC1 channels (NTC thermistor, supply
voltage) and the twelve channels of the ADS7952 external ADC. Channels are registered from the pin map, so the names
and the scaling come from there. Source: [`AdcManager.h`](../../lib/managers/AdcManager.h).

## Channels

| Name (for `ReadVoltage`) | Source | Notes |
| --- | --- | --- |
| `ADC_NTC_THERMISTOR` | ESP32 ADC1, 12 dB attenuation | Read by `TemperatureManager` for the NTC |
| `ADC_VBUS_MONITOR` | ESP32 ADC1, 12 dB attenuation | Returned already multiplied by the divider (11) |
| `ADC_ADS7952_0_CH0` ... `ADC_ADS7952_0_CH11` | ADS7952 device 0, 2.5 V reference | Only with `HARDFOC_ADS7952_SUPPORT`; CH8 carries the MCP9700 |

The full list with channel numbers is on the [pin map](../hardware/pin-map.md).

## API

| Call | Notes |
| --- | --- |
| `ReadVoltage(name or HfFunctionalAdcChannel, float& volts, samples = 1, time_between_samples = 0)` | Volts, divider applied; with `samples > 1` the driver averages |
| `ReadRaw(name or channel, uint32_t& counts, samples = 1, time_between_samples = 0)` | Raw counts, no scaling |
| `Get(name)`, `Get(HfFunctionalAdcChannel)` | The `BaseAdc*` behind a channel (shared by all channels of one chip) |
| `Contains(name)`, `Size()` | Lookup helpers |
| `GetSystemDiagnostics(AdcSystemDiagnostics&)`, `DumpStatistics()` | Channel counts per chip, operation counters |

## Example: supply voltage

```cpp
#include "api/Flux.h"
#include "managers/AdcManager.h"

auto& adc = Flux::GetInstance().adc;
float vbus = 0.0f;
if (adc.IsInitialized() &&
    adc.ReadVoltage(HfFunctionalAdcChannel::ESP32_VBUS_MONITOR, vbus, 8) == hf_adc_err_t::ADC_SUCCESS) {
    // vbus is the supply voltage in volts (8-sample average, divider applied)
}
```

## Diagnostics

`GetSystemDiagnostics()` reports registered channels, `channels_by_chip[0]` (ESP32) and `[1]` (ADS7952), and
operation counters. Per-channel access and error counts are logged by `DumpStatistics()`.

## Pitfalls

- **Fail-closed start-up.** If the ADS7952 or the MCU ADC does not come up, the manager stays uninitialised
  ([ADR 0005](../adr/0005-no-heap-fail-closed.md)): every read returns `ADC_ERR_NOT_INITIALIZED` and `Get()` returns
  `nullptr`. `GetManagerError()` says why, and `IsAds7952Ready(0)` / `IsInternalAdcReady()` say which part is
  missing. Check the log line for the ADC SPI bus.
- **`GetLastError()`** holds the last failed read; it is not cleared by a later success.
- **Names are strings.** In a fast loop use the enum overload, which indexes directly instead of searching.
