# Managers

A manager owns one function of the board. You reach each one through the `Flux` object (`flux.valves`) or its own
`GetInstance()`; both give the same singleton. All managers share the same shape: `EnsureInitialized()`,
`IsInitialized()`, `GetLastError()`, `GetSystemDiagnostics(...)` and `DumpStatistics()`.

| Member | Manager | Does | Start-up |
| --- | --- | --- | --- |
| `flux.comms` | [CommChannelsManager](comms.md) | Valve SPI bus, ADC SPI bus, CAN, UARTs on demand | Required |
| `flux.nvs` | [NvsManager](nvs.md) | Three NVS namespaces: application data, calibration, configuration | Required |
| `flux.gpio` | [GpioManager](gpio.md) | Every GPIO-category pin of the pin map, by name or enum | Required |
| `flux.valves` | [ValveManager](valves.md) | 8 solenoid channels (MAX22200) and 6 proportional channels (TLE92466ED) | Best effort |
| `flux.adc` | [AdcManager](adc.md) | ESP32 ADC channels and the ADS7952 external ADC | Best effort |
| `flux.leds` | [LedManager](leds.md) | The WS2812 status LED: colours, brightness, status animations | Best effort |
| `flux.temp` | [TemperatureManager](temperature.md) | ESP32 internal sensor, NTC thermistor, MCP9700 | Best effort |

"Required" and "best effort" refer to [what happens when initialisation fails](../architecture/initialization.md).
Which handler each manager owns is on the [ownership](../architecture/ownership.md) page.

Each page has the same sections: what the manager is for, its API, an example, its diagnostics, and its pitfalls.
The full signatures are in the [API reference](../api-reference.md).
