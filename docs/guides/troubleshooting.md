# Troubleshooting

Problems people hit in their first days with the board, what causes them and what to do.

## Build

| Symptom | Cause | Fix |
| --- | --- | --- |
| `build_app.sh: No such file or directory` | The `examples/esp32/scripts` submodule is missing | `git submodule update --init --recursive` |
| CMake cannot find `lib/core/...` headers | `lib/core` or its nested submodules are missing | Same as above, from the repository root |
| `APP_TYPE not defined` | `idf.py build` was run directly | Build through `./scripts/build_app.sh <app> <type>` |
| `No supported board variant defined!` | Building hf-core without the HAL's CMake | Build through this repository; its `CMakeLists.txt` defines `HARDFOC_BOARD_FLUX_V1` |
| Wrong target errors | An old build folder for another target | Delete the app's build folder; the target is `esp32s3` |

## Start-up

| Symptom | Cause | Fix |
| --- | --- | --- |
| `NVS ... FAIL`, `EnsureInitialized()` returns false | NVS partition full or written by another firmware | Erase flash once: `idf.py -p PORT erase-flash` (this erases everything) |
| `Valves WARN` | Neither valve driver answered | Check the 24 V valve supply and the valve SPI wiring; the log says which chip failed |
| Only one valve chip works | The other chip failed its own start-up | `flux.valves.GetSystemDiagnostics(...)` shows `max22200_initialized` / `tle92466_initialized`; the other chip's channels return `VALVE_ERR_DRIVER_NOT_AVAILABLE` |
| `ADC WARN` or no `ADC_ADS7952_*` channels | The ADS7952 did not answer on the ADC SPI bus | Check the ADC SPI pins on the [pin map](../hardware/pin-map.md); the ESP32 ADC channels still work |
| No `MCP9700_THERMISTOR` | Needs a working ADS7952 channel 8 | Fix the ADS7952 first |

## Valves

| Symptom | Cause | Fix |
| --- | --- | --- |
| `EnableChannel()` succeeds but the valve does not move | Valve supply off, or the channel was never configured | Check 24 V; call `ConfigureSolenoidCdr/Vdr` or `SetProportionalCurrent` first |
| `VALVE_ERR_CHANNEL_TYPE_MISMATCH` | A solenoid call on a `PROP_CH*` channel or the reverse | Use the call for the channel's chip (see [ValveManager](../managers/valves.md)) |
| Proportional current reads 0 | Feedback frozen or supply under-voltage | Start-up clears the freeze; check `GetProportionalFaults()` for a supply fault |

## LED and GPIO

| Symptom | Cause | Fix |
| --- | --- | --- |
| LED animation stuck on the first frame | Nobody calls `UpdateAnimation()` | Call it from a periodic task |
| `GPIO_ERR_PIN_NOT_FOUND` | Typo, or the pin is not a GPIO-category pin | Use the exact name from the [pin map](../hardware/pin-map.md) |
| A fault input always reads the same | The pin is still an output | `SetDirection(name, HF_GPIO_DIRECTION_INPUT)`; `ValveManager` does this for the two fault pins |

## Still stuck

Run `flux_api_test` and read its log top to bottom: it initialises each manager and prints what it found. Then call
`Flux::GetInstance().DumpSystemStatistics()` from your own code to log every manager's state.
