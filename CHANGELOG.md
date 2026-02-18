# Changelog — HardFOC Flux V1 HAL

All notable changes to this project will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.0] — 2025-06-26

### Added

- **Manager Deinitialize()**: Every heap-owning manager now exposes
  `bool Deinitialize() noexcept` for clean teardown and resource recovery.
  Affected managers: `AdcManager`, `CommChannelsManager`, `GpioManager`,
  `TemperatureManager`, `ValveManager`.
- **Component-handler documentation**: Six new comprehensive READMEs under
  `docs/component-handlers/` covering ADC, CommChannels, GPIO, LED,
  Temperature, and Valve managers.
- **Development guide index**: `docs/development/README.md` pointing to
  authoritative Vortex dev guides with Flux-specific conventions.
- **On-target test suite**: Three new test files under `examples/esp32/main/`:
  `valve_cycling_test.cpp`, `gpio_adc_test.cpp`, `led_temp_test.cpp`
  using the shared `TestFramework.h` harness.
- **CI app entries**: `app_config.yml` updated with test app registrations.

### Fixed

- **CRITICAL — `LedManager::StopAnimation()` crash**: Added null-pointer
  guard before dereferencing `led_strip_` — prevented nullptr crash when
  called before initialization.
- **Init-failure memory leaks**: All managers now `delete` heap-allocated
  driver objects when their `Initialize()` call fails, preventing orphaned
  memory when `initialized_` is never set to `true`:
  - `AdcManager` — frees `EspAdc` on init failure.
  - `TemperatureManager` — frees `EspTemperature` and `NtcTemperatureHandler`.
  - `ValveManager` — frees `Max22200Handler` and `Tle92466edHandler`.
  - `CommChannelsManager` — frees `EspCan` / `EspI2cBus` when the hard-required
    SPI bus fails.
- **`GpioManager::Get()` init guard**: Both `Get(string_view)` and
  `Get(HfFunctionalGpioPin)` overloads now early-return `nullptr` when the
  manager has not been initialized, giving callers correct error semantics.

---

## [1.0.0] — 2025-06-01

Initial release of the HardFOC Flux V1 HAL for ESP32-C6.

### Included

- Six managers: ADC, CommChannels, GPIO, LED, Temperature, Valve.
- Flux API façade (`Flux.h` / `Flux.cpp`).
- Board pin configuration (`pincfg`).
- ESP-IDF 5.4+ build integration.
