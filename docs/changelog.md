# Changelog

What changed in the Flux V1 HAL, newest first. Releases are tagged `vX.Y.Z` and listed on
[GitHub releases](https://github.com/hardfoc/hf-hal-flux-v1/releases); no release has been tagged yet.

## Unreleased

### Fixed

- hf-core is bumped to the bus-error adapters (hf-core `fix/bus-error-status`): the MAX22200 adapter's `GpioSet()`
  returns the GPIO result and the driver reports a CMD or ENABLE pin it could not drive. A valve call whose CMD pin
  is stuck now fails (`GPIO_ERROR`) instead of reporting success after sending the Command Register byte as a data
  frame. The Alicat BASIS-2 and FDO2 UART adapters report a failed write as `SerialError`. The host chip models
  needed no change: they model the chips at the SPI and GPIO level, below the adapters.
- New host test `test_valve_bus_errors`.

### Changed

- The documentation builds with the shared HardFOC theme (`hardfoc-mkdocs-theme`); the copied template, hooks and
  figure toolkit are gone. The Doxyfile moved to the repository root.
- The documentation follows the HAL page set: board card, quickstart, bring-up and troubleshooting, changelog.
- hf-core is bumped to main plus the hf-utils-rtos-wrap FreeRTOS fixes: `OsQueue` holds exactly N items, event-group
  waits report timeouts and clear bits only on request, and tick conversions follow `configTICK_RATE_HZ`. The HAL
  uses none of the changed APIs, so no manager changes.

### Added

- `Flux` facade with seven managers: communication channels, NVS, GPIO, valves (MAX22200 and TLE92466ED), ADC
  (ESP32 and ADS7952), LED and temperature.
- Pin map, examples catalogue and figures generated from the code and checked in CI.
