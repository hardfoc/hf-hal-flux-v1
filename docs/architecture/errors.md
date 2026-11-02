# Errors and diagnostics

The HAL does not throw. Every call is `noexcept` and reports failure through its return value. This page lists the
error types, where to read the last error, and the system-level diagnostics.

## Rules of thumb

1. **Check the return value.** Most methods are `[[nodiscard]]`; the compiler warns if you drop a result.
2. A method that returns `bool` returns `false` on failure; a method that returns a pointer returns `nullptr` when
   the manager is not initialised or the thing you asked for does not exist.
3. Every manager has `GetLastError()`, but it is only as good as the manager's bookkeeping (see the table): prefer
   the value a call returns.

## Error types by manager

| Manager | Error type | To text | `GetLastError()` updated |
| --- | --- | --- | --- |
| `NvsManager` | `hf_nvs_err_t` | `HfNvsErrToString()` | On initialisation failure |
| `CommChannelsManager` | `CommError` | `CommErrorToString()` | Yes |
| `GpioManager` | `hf_gpio_err_t` | `HfGpioErrToString()` | Yes |
| `ValveManager` | `hf_valve_err_t` | `HfValveErrToString()`, `HfValveErrOk()` | **No**: always `VALVE_SUCCESS` today |
| `AdcManager` | `hf_adc_err_t` | `HfAdcErrToString()` | **No**: always `ADC_SUCCESS` today |
| `LedManager` | `LedError` | `LedErrorToString()` | Yes |
| `TemperatureManager` | `TemperatureError` | `TemperatureErrorToString()` | Yes |

`hf_valve_err_t` groups its values: 1-6 are usage errors (not initialised, wrong channel type, bad parameter), 10-14
SPI errors, 20-27 errors reported by a driver chip, 30-31 locking and dependency errors.
`TleDriverErrorToValveError()` and `MaxDriverStatusToValveError()` translate the chip drivers' own codes.

## System diagnostics

All of these are on the `Flux` object and need a successful `EnsureInitialized()` first.

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
    Because `ValveManager` and `AdcManager` do not record their last error, the health snapshot reports them as
    healthy whenever they are initialised.
