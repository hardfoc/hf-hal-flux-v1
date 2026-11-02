# Ownership

Who creates what, who owns it, and how long it lives. In one sentence: every manager is a function-local static
singleton that lives for the whole program, and each manager creates and owns the handlers and bus objects it needs
during its own initialisation.

<!-- figure: manager-dependencies -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../diagrams/manager-dependencies.dark.svg">
  <img src="../diagrams/manager-dependencies.svg"
    alt="Graph from each manager to the hf-core handlers it uses and from each handler to the chip driver it
    wraps: ValveManager to the MAX22200 and TLE92466ED handlers, AdcManager to the ADS7952 handler,
    TemperatureManager to the NTC and MCP9700 handlers. CommChannelsManager, GpioManager, LedManager and
    NvsManager use no handler.">
</picture>
<!-- /figure -->

## Manager map

<!-- generated: manager-map -->
From the `#include` lines of
[`lib/managers`](../../lib/managers) and the hf-core handlers; do not edit.

| Manager | Uses managers | hf-core handlers (driver) | Drivers used directly | Interface classes |
| --- | --- | --- | --- | --- |
| [`AdcManager`](../../lib/managers/AdcManager.h) | `CommChannelsManager` | `ads7952` (hf-ads7952) | - | `BaseAdc` |
| [`CommChannelsManager`](../../lib/managers/CommChannelsManager.h) | - | - | - | `BaseCan`, `BaseSpi`, `BaseUart`, `BaseUsbSerialJtag` |
| [`GpioManager`](../../lib/managers/GpioManager.h) | - | - | - | `BaseGpio` |
| [`LedManager`](../../lib/managers/LedManager.h) | - | - | - | - |
| [`NvsManager`](../../lib/managers/NvsManager.h) | - | - | - | `BaseNvs` |
| [`TemperatureManager`](../../lib/managers/TemperatureManager.h) | `AdcManager` | `mcp9700` (hf-mcp9700), `ntc` (hf-ntc-thermistor) | - | `BaseTemperature` |
| [`ValveManager`](../../lib/managers/ValveManager.h) | `CommChannelsManager`, `GpioManager` | `max22200` (hf-max22200), `tle92466ed` (hf-tle92466ed) | - | - |
<!-- /generated -->

## Singletons

`Flux` and every manager follow the same pattern:

- `GetInstance()` returns a function-local `static` object (constructed on first use; C++ guarantees that
  construction is thread-safe).
- Copy and move are deleted, so there is exactly one of each.
- `Flux` holds **references** to the managers (`flux.valves` is `ValveManager::GetInstance()`), so
  `flux.valves` and `ValveManager::GetInstance()` are the same object. Use whichever reads better.

## Who owns which object

| Object | Owner | Created | Notes |
| --- | --- | --- | --- |
| Valve SPI bus, ADC SPI bus, CAN controller | `CommChannelsManager` | In its `EnsureInitialized()` | Objects in the platform binding's static slots; other managers borrow raw `BaseSpi*` / `BaseCan*` pointers |
| UART ports | `CommChannelsManager` | On the first `AcquireUart()` for a port | Up to `kMaxUartPorts` (3), binding slots, cached for later calls |
| One GPIO object per GPIO-category pin | `GpioManager` | In its `EnsureInitialized()` | Binding slot per functional pin; fixed table sized from the pin map |
| `Max22200Handler`, `Tle92466edHandler` | `ValveManager` | In its `EnsureInitialized()` | Members (`std::optional`), constructed in place; each borrows an SPI device from comms and its control pins from `GpioManager` |
| MCU ADC (ADC1), one `Ads7952Handler` per ADS7952 device | `AdcManager` | In its `EnsureInitialized()` | ADC1 in a binding slot; ADS7952 handlers are members (`std::array` of `std::optional`, four slots); channels registered from the pin map |
| MCU die sensor, `NtcTemperatureHandler`, `Mcp9700TemperatureHandler` | `TemperatureManager` | In its `EnsureInitialized()` | Die sensor in a binding slot; the two thermistor handlers are members and borrow ADC channels from `AdcManager` |
| WS2812 strip | `LedManager` | In its `EnsureInitialized()` | The binding drives the WS2812 RMT driver (`LedStrip*`); the manager keeps the animation state |
| Three NVS namespaces (`flux`, `flux_cal`, `flux_cfg`) | `NvsManager` | Binding slots, referenced at construction, opened in its `EnsureInitialized()` | `App()`, `Calibration()`, `Config()` return them as `BaseNvs&` |

A borrowed pointer stays valid for as long as its owner is initialised, which in practice is the whole program.
Do not keep a pointer across a `Shutdown()` / `Deinitialize()`.

## Memory

The managers use fixed-size `std::array` tables, sized at compile time from the pin map, for everything that has a
known count (pins, ADC channels, valve channels, SPI devices). Pin, bus, port, NVS and LED objects live in the
platform binding's static slots ([Platform bindings](bindings.md)); the hf-core handlers are manager members,
constructed in place during initialisation. Nothing in the HAL library allocates from the heap, and
`tools/lint/lint_hal_alloc.sh` keeps it that way in CI ([ADR 0005](../adr/0005-no-heap-fail-closed.md)).
