# Changelog

What changed in the Flux V1 HAL, newest first. Releases are tagged `vX.Y.Z` and listed on
[GitHub releases](https://github.com/hardfoc/hf-hal-flux-v1/releases); no release has been tagged yet.

## Unreleased

### Changed

- The documentation builds with the shared HardFOC theme (`hardfoc-mkdocs-theme`); the copied template, hooks and
  figure toolkit are gone. The Doxyfile moved to the repository root.
- The documentation follows the HAL page set: board card, quickstart, bring-up and troubleshooting, changelog.

### Added

- `Flux` facade with seven managers: communication channels, NVS, GPIO, valves (MAX22200 and TLE92466ED), ADC
  (ESP32 and ADS7952), LED and temperature.
- Pin map, examples catalogue and figures generated from the code and checked in CI.
