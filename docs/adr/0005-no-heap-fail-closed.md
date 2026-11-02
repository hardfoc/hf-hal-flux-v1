# ADR 0005: No heap in the HAL; managers fail closed and report through one error catalog

| | |
| --- | --- |
| Status | Accepted |
| Date | 2026-11-02 |

## Context

[ADR 0002](0002-static-allocation.md) allowed handlers and bus objects on the heap, created once during
initialisation, and listed exceptions (`std::make_unique` in `GpioManager` and `LedManager`). In practice nothing
checked the rule, and three more problems sat next to it:

- **Partial bring-up reported success.** `ValveManager` succeeded with either driver IC, `AdcManager` with any one
  channel, `TemperatureManager` with any one sensor, `GpioManager` with any one pin. A board with a dead TLE92466ED
  reported "valves initialised".
- **Errors were not recorded.** `ValveManager` and `AdcManager` never set their last error, so the health snapshot
  reported them healthy after failures, and every manager had its own enum, so codes could not be compared.
- **`Flux::Shutdown()` skipped `ValveManager`**, leaving channels as they were.

`AdcManager` also declared its ADS7952 handler array under the HAL's private `HARDFOC_ADS7952_SUPPORT` macro, so the
product, which does not define it, saw a different class layout from the HAL (an ODR violation).

## Options

1. **Keep ADR 0002 and fix the bookkeeping.** Leaves heap use unchecked and success after partial bring-up.
2. **Static storage everywhere, fail-closed managers, one error catalog**, as in pw-hal's coding standards
   (`coding-standards-managers.md` §2): two error lanes, `bool` + `ManagerError` for managers and `HalResult` for
   facades.

## Decision

Option 2.

- **No heap.** Every handler is a manager member (`std::optional`, constructed in place during
  `EnsureInitialized()`); every pin, bus, port, NVS and LED object is in a platform-binding slot
  ([ADR 0004](0004-platform-bindings.md)). `tools/lint/lint_hal_alloc.sh` fails CI on `make_unique`, `unique_ptr`,
  `new T`, `malloc`, `std::vector`, `std::string`, `std::function` and `std::map` anywhere in `lib/api` or
  `lib/managers`, except in the host test binding. ESP-IDF drivers that allocate their own queues (SPI master, TWAI,
  UART, RMT) do so inside `EnsureInitialized()` or, for UARTs, inside the application's `AcquireUart()` at boot.
- **Fail closed.** A manager's `EnsureInitialized()` succeeds only when every part it is responsible for came up:
  both valve driver ICs; every GPIO-category pin; the MCU ADC and every ADS7952 in the pin map; all three NVS
  namespaces; the valve SPI bus and both valve chip selects. Otherwise it stays
  uninitialised, refuses its operations, and records why. Part readiness stays queryable for diagnosis
  (`IsBankReady()`, `IsAds7952Ready()`, `IsSensorReady()`, `IsPressureBusReady()`). The exception is
  `TemperatureManager`: its three sensors are independent monitors with a designed fallback (MCP9700, NTC, die
  sensor), so it runs degraded with any sensor that came up, but records the error and never reports healthy.
- **One catalog.** Every manager has `GetManagerError()` returning `flux::hal::ManagerError`
  (`lib/api/flux_hal/flux_hal_types.hpp`); `Flux::CollectDiagnostics()` aggregates them with part readiness, and
  `Flux::GetLastError()` names the first required manager that failed. The older per-manager enums stay on the
  operation calls and their `GetLastError()`, now set on every failure, until each manager is next reworked.
- **Shutdown** de-initialises `ValveManager`, which turns every channel off before releasing its handlers.

`Flux::EnsureInitialized()` keeps its split: NVS, comms and GPIO are required, the rest optional. A board with a
missing valve driver still boots and logs; it just has no usable valve manager.

## Consequences

- RAM for every handler is reserved at link time (`.bss`), whether or not the part answers. Heap use after start-up
  is limited to what ESP-IDF itself does.
- A bench with one valve driver fitted no longer gets the other bank through `ValveManager`; the bank that came up
  is reachable through its handler accessor for diagnosis only.
- This supersedes ADR 0002's allowance for heap-allocated handlers; ADR 0002's fixed tables stand.
