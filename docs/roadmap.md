# Roadmap

Where the Flux V1 HAL is going, and the known gaps on the way. Items in "next" come from the code as it is today;
nothing here is a promise of a date.

## Now

- One `Flux` facade and seven managers covering every chip on the board ([Managers](managers/index.md)).
- Pin map, examples catalogue and figures generated from the code, checked in CI ([Testing and CI](testing.md)).
- Builds for the ESP32-S3 with ESP-IDF release/v5.5. ESP-IDF code is confined to the platform binding
  (`lib/managers/bsp_bind/esp32s3`), checked by a boundary lint, and the managers also build on a host
  ([Platform bindings](architecture/bindings.md)).

## Next: close the known gaps

| Gap | Where | Why it matters |
| --- | --- | --- |
| `ValveManager` and `AdcManager` never record their last error | `ValveManager.cpp`, `AdcManager.cpp` | The health snapshot reports them healthy after a failed call ([Errors](architecture/errors.md)) |
| `Flux::Shutdown()` skips `ValveManager` | `Flux.cpp` | Valves stay as they were on shutdown ([Initialisation](architecture/initialization.md)) |
| `Flux::GetSystemWarnings()` is always empty | `Flux.h` | Nothing calls the internal `AddSystemWarning()` |
| GPIO pins all start as outputs | `GpioManager.cpp` | Fault inputs are outputs until `ValveManager` reconfigures them |
| No host unit tests | - | Logic such as channel resolution and error mapping is only tested on hardware |

## Later: new transports and MCUs

- **CAN protocols.** The HAL already builds the hf-core CANopen helpers (`HF_CORE_ENABLE_UTILS_CANOPEN` in
  `CMakeLists.txt`) and exposes the CAN bus as `BaseCan*` through `CommChannelsManager::GetCanBus()`. A CANopen
  device profile for Flux would sit above that, in the application, without changing the managers. The Vortex V1
  board is getting the first HardFOC CANopen node, and its design will be reused here where it fits.
- **Another MCU and CAN FD.** The interface-wrap boundary in [ADR 0001](adr/0001-mcu-agnostic-boundary.md) is what a
  port to an STM32 with CAN FD would use: new `Base*` implementations created by the managers, a pin map for the new
  board, and the same handlers and application code above.
