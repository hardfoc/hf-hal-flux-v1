# Initialisation

`Flux::EnsureInitialized()` brings every manager up, one at a time, in a fixed order. Call it once, early in
`app_main()`, before you touch any manager.

<!-- figure: init-order -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../diagrams/init-order.dark.svg">
  <img src="../diagrams/init-order.svg"
    alt="Initialisation order: NvsManager, CommChannelsManager, GpioManager are required; then ValveManager,
    AdcManager, LedManager, TemperatureManager are best effort and only produce warnings if they fail.">
</picture>
<!-- /figure -->

## The order

<!-- generated: init-order -->
From [`Flux.cpp`](../../lib/api/Flux.cpp)
(`Flux::InitializeAllComponents`); do not edit.

| Step | Manager | Member | If it fails |
| ---: | --- | --- | --- |
| 1 | `NvsManager` | `flux.nvs` | **initialisation fails** (`EnsureInitialized()` returns false) |
| 2 | `CommChannelsManager` | `flux.comms` | **initialisation fails** (`EnsureInitialized()` returns false) |
| 3 | `GpioManager` | `flux.gpio` | **initialisation fails** (`EnsureInitialized()` returns false) |
| 4 | `ValveManager` | `flux.valves` | warning; the system runs degraded |
| 5 | `AdcManager` | `flux.adc` | warning; the system runs degraded |
| 6 | `LedManager` | `flux.leds` | warning; the system runs degraded |
| 7 | `TemperatureManager` | `flux.temp` | warning; the system runs degraded |
<!-- /generated -->

The order follows the dependencies: storage first, so configuration and calibration are available early; then the
buses; then the GPIO pins the valve drivers need; then the managers that use them. `TemperatureManager` comes after
`AdcManager` because the NTC and MCP9700 sensors are read through ADC channels.

## What happens when a step fails

- **Required steps (NVS, comms, GPIO).** If one fails, `EnsureInitialized()` logs the failure, stops and returns
  `false`. `IsInitialized()` stays `false`, and calling `EnsureInitialized()` again retries from the start.
- **Best-effort steps (valves, ADC, LEDs, temperature).** A failure is logged as a `WARN` row in the start-up table
  and the manager stays unavailable; the remaining steps still run and `EnsureInitialized()` returns `true`. Check
  `flux.valves.IsInitialized()` (or the [diagnostics](errors.md)) before relying on a best-effort manager.

This lets a bench with only some chips fitted, or with the 24 V valve supply off, still come up and log.

Inside every manager the rule is the opposite: **a manager fails closed** ([ADR 0005](../adr/0005-no-heap-fail-closed.md)).
Its `EnsureInitialized()` succeeds only when every part it is responsible for came up; otherwise it stays
uninitialised, refuses its operations and records the reason in `GetManagerError()`.

| Manager | Must all come up |
| --- | --- |
| `NvsManager` | The `flux`, `flux_cal` and `flux_cfg` namespaces |
| `CommChannelsManager` | The valve SPI bus and both valve chip selects (the pressure bus and CAN are reported, not required) |
| `GpioManager` | Every GPIO-category pin of the pin map |
| `ValveManager` | Both driver ICs: TLE92466ED and MAX22200 |
| `AdcManager` | The MCU ADC (if the pin map uses it) and every ADS7952 in the pin map |
| `LedManager` | The WS2812 strip |
| `TemperatureManager` | At least one sensor; runs **degraded** (initialised, error recorded, `IsDegraded()`) when one of the die sensor, NTC and MCP9700 is missing |

Each manager still tries every part, so the log and `Flux::CollectDiagnostics()` name every missing one, and part
readiness stays queryable after a failure (`valves.IsBankReady()`, `adc.IsAds7952Ready()`,
`temp.IsSensorReady()`, `comms.IsPressureBusReady()`). A valve bank that came up while the other did not is
reachable through its handler accessor for diagnosis, never through the channel calls.

## Calling a manager directly

Every manager also has its own `EnsureInitialized()`, and the managers that depend on others call those first (for
example `ValveManager` calls `CommChannelsManager` and `GpioManager`). So code that uses one manager on its own
works, but going through `Flux::EnsureInitialized()` gives you the logged start-up table and the system diagnostics.

## Start-up log

A healthy start prints one row per step, with its time:

```text
  1  NVS             OK      ...
  2  Comms           OK      ...
  3  GPIO            OK      ...
  4  Valves          OK      ...
  5  ADC             OK      ...
  6  LEDs            OK      ...
  7  Temp            OK      ...
Flux API initialized in ... ms
```

`FAIL` marks a required step that failed, `WARN` a best-effort step that failed.

## Shutting down

`Flux::Shutdown()` de-initialises the managers in reverse order (temperature, LEDs, ADC, valves, GPIO, comms, NVS).
`ValveManager::Deinitialize()` turns every channel off before it releases the driver handlers. A failed manager can
be retried: its `EnsureInitialized()` releases whatever the failed attempt left up and starts again.
