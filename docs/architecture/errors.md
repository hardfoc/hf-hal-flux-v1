# Errors and diagnostics

The HAL does not throw. Every call is `noexcept` and reports failure through its return value. This page lists the
error types, where to read the last error, and the system-level diagnostics.

## Rules of thumb

1. **Check the return value.** Most methods are `[[nodiscard]]`; the compiler warns if you drop a result.
2. A method that returns `bool` returns `false` on failure; a method that returns a pointer returns `nullptr` when
   the manager is not initialised or the thing you asked for does not exist.
3. Every manager has two error lanes, as in pw-hal ([ADR 0005](../adr/0005-no-heap-fail-closed.md)):
   `GetManagerError()` says why bring-up failed, in the shared `flux::hal::ManagerError` catalog
   ([`flux_hal_types.hpp`](../../lib/api/flux_hal/flux_hal_types.hpp)); `GetLastError()` holds the last failed
   operation in the manager's own enum. Facades return `flux::hal::HalResult`.

## Bring-up errors (shared catalog)

| `ManagerError` | Meaning |
| --- | --- |
| `None` | The manager is up |
| `NotInitialized` | Not attempted yet, or shut down |
| `BindFailed` | A bus, pin or platform object could not be created (missing in the pin map, or the binding refused it) |
| `HardwareFault` | A fitted chip did not answer or reported a fault during bring-up |
| `DependencyNotReady` | A manager this one needs is down |
| `InvalidArgument`, `Busy`, `Unsupported` | Reserved for facade calls |

Managers fail closed: `IsInitialized()` is `true` only when `GetManagerError()` is `None`. The one exception is
`TemperatureManager`, whose sensors are independent monitors with a fallback chain: it runs degraded with any
sensor that came up, keeps the reason in `GetManagerError()` and reports `IsDegraded()`.

## Operation errors by manager

| Manager | Error type | To text | `GetLastError()` updated |
| --- | --- | --- | --- |
| `NvsManager` | `hf_nvs_err_t` | `HfNvsErrToString()` | On initialisation failure |
| `CommChannelsManager` | `CommError` | `CommErrorToString()` | Yes |
| `GpioManager` | `hf_gpio_err_t` | `HfGpioErrToString()` | Yes |
| `ValveManager` | `hf_valve_err_t` | `HfValveErrToString()`, `HfValveErrOk()` | Yes, on every failed call and on failed bring-up |
| `AdcManager` | `hf_adc_err_t` | `HfAdcErrToString()` | Yes, on every failed read and on failed bring-up |
| `LedManager` | `LedError` | `LedErrorToString()` | Yes |
| `TemperatureManager` | `TemperatureError` | `TemperatureErrorToString()` | Yes |

`hf_valve_err_t` groups its values: 1-6 are usage errors (not initialised, wrong channel type, bad parameter), 10-14
SPI errors, 20-27 errors reported by a driver chip, 30-31 locking and dependency errors.
`TleDriverErrorToValveError()` and `MaxDriverStatusToValveError()` translate the chip drivers' own codes.

## System diagnostics

`Flux::CollectDiagnostics(FluxHalDiagnostics&)` works at any time, including after a failed
`EnsureInitialized()`: every manager in init order with `required`, `initialized` and its `ManagerError`, the first
error and which manager reported it, and the readiness of every fitted part (MAX22200, TLE92466ED, pressure bus,
ADS7952, MCU ADC, CAN, die sensor, NTC, MCP9700, status LED). `Flux::GetLastError()` returns the error of the first
required manager that failed.

```cpp
FluxHalDiagnostics d;
Flux::GetInstance().CollectDiagnostics(d);
if (!d.all_managers_up) {
    ESP_LOGW("app", "first error: %s in %s; MAX22200=%d TLE92466ED=%d ADS7952=%d",
             flux::hal::ToString(d.first_error), d.first_error_manager,
             d.max22200_ready, d.tle92466_ready, d.ads7952_ready);
}
```

The older calls below need a successful `EnsureInitialized()` first.

| Call | Gives you |
| --- | --- |
| `GetSystemDiagnostics(FluxSystemDiagnostics&)` | Which managers initialised, how many failed (and their names), start-up time, uptime; `system_healthy` is true when none failed |
| `GetComponentInitializationStatus()` | `std::array<bool, 7>` in manager order |
| `GetFailedComponents(out, max)` | Names of managers whose initialisation failed |
| `CollectManagerHealth(ManagerHealthSnapshot&)` | Per manager: initialised, last error code (as `uint8_t`), and an overall `all_healthy` |
| `PerformHealthCheck()` | Logs the health snapshot and returns `all_healthy` |
| `DumpSystemStatistics()` | Logs every manager's statistics |
| `GetFreeHeapBytes()`, `GetLargestFreeHeapBlock()`, `GetLastResetReason()` | Heap and reset information |

Each manager also has its own `GetSystemDiagnostics(...)` and `DumpStatistics()`; see the manager pages.

```cpp
ManagerHealthSnapshot health;
if (Flux::GetInstance().CollectManagerHealth(health) && !health.all_healthy) {
    for (size_t i = 0; i < health.count; ++i) {
        const auto& e = health.entries[i];
        if (!e.initialized || e.has_error) {
            ESP_LOGW("app", "%s: init=%d error=%u", e.name, e.initialized, e.last_error_code);
        }
    }
}
```

!!! note "Known gaps"
    `GetSystemWarnings()` always returns 0 entries today: the facade has an internal warning list but nothing adds
    to it. Best-effort start-up failures are visible as `WARN` rows in the log and through `GetFailedComponents()`.
    `CollectManagerHealth()` casts each manager's own operation enum to `uint8_t`, so its codes are not comparable
    across managers; use `CollectDiagnostics()` for bring-up state.
