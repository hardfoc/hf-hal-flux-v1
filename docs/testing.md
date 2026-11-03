# Testing and CI

What runs on a pull request, what each check means, and how to run it on your machine first. All workflows are in
[`.github/workflows`](../.github/workflows); most call reusable workflows in
[hf-general-ci-tools](https://github.com/hardfoc/hf-general-ci-tools) or
[hf-espidf-ci-tools](https://github.com/hardfoc/hf-espidf-ci-tools).

## Workflows

| Workflow | File | Runs on | Checks |
| --- | --- | --- | --- |
| ESP32 Examples Build CI | `esp32-examples-build-ci.yml` | push, pull request | Builds every app with `ci_enabled: true` in `app_config.yml`, Debug and Release, with ESP-IDF release/v5.5 |
| C++ Lint CI | `ci-cpp-lint.yml` | push, pull request | clang-format and clang-tidy with `_config/.clang-format` and `_config/.clang-tidy` |
| C++ Analysis CI | `ci-cpp-analysis.yml` | push, pull request | cppcheck on `lib/managers` and `lib/api` (C++20) |
| Docs Publish CI | `ci-docs-publish.yml` | push to main, `v*` tags, pull request | `hardfoc-docs check` (figures and generated tables current), strict MkDocs build, Doxygen under `api/`, offline link check of the built site, Lighthouse budget, phone and desktop screenshots (artifact `docs-screenshots`); publishes on main and tags |
| Docs Link Check CI | `ci-docs-linkcheck.yml` | push, pull request | lychee over the Markdown files, config `_config/lychee.toml` |
| Markdown Lint CI | `ci-markdown-lint.yml` | push, pull request | markdownlint, config `_config/.markdownlint.json` |
| YAML Lint CI | `ci-yaml-lint.yml` | push, pull request | yamllint, config `_config/.yamllint` |
| HAL Boundary CI | `ci-hal-boundary.yml` | push, pull request | `tools/lint/lint_hal_boundary.sh` (no ESP-IDF, FreeRTOS or `Esp*` code outside `lib/managers/bsp_bind/<family>/`; shrink-only allowlist checked against the PR base); `tools/lint/lint_hal_alloc.sh` (no heap or heap-backed type in the HAL library); host build of the managers against `bsp_bind/host` and the manager tests in `tests/host` (GCC, Clang, Clang with UBSan) |
| Release | `release.yml` | `v*` tags | Builds the release artifacts |

The examples are integration tests: they run on a board, not in CI. CI proves they compile; a person flashes them.
The manager logic is unit-tested on a host, below.

## Host tests

[`tests/host`](../tests/host) builds the facade and the managers against the host family of the platform binding
([`bsp_bind/host`](../lib/managers/bsp_bind/host), over hf-core's `hf_core_host_mocks`) and runs one executable per
manager. The chips are register-level SPI models in [`FluxChipModels.h`](../tests/host/FluxChipModels.h), so the real
hf-core handlers and chip drivers run their unmodified bring-up and channel code:

| Model | What it answers |
| --- | --- |
| `Max22200Model` | Two-phase protocol (command byte with CMD high, then 8- or 32-bit data), STATUS / CFG / FAULT registers, fault flags on SDO, the CFG_CHx fields as the chip reads them (VDR duty in percent), an absent part (MISO high) |
| `Tle92466Model` | 32-bit frames with the SAE J1850 CRC, the one-frame reply pipeline, 16- and 22-bit reply widths, write-1-to-clear diagnosis registers, ICVID, critical-fault replies (reply mode 10B, no CRC), an absent part |
| `Ads7952Model` | 16-bit frames, manual channel select, channel id in the reply, an absent part |

| Test | Proves |
| --- | --- |
| `test_valve_manager` | `kValveChannelMap` is a bijection onto MAX22200 OUT0-7 and TLE92466ED OUT0-5; each channel switches only its own output on the wire; currents land on the mapped TLE channel; the bank mask splits; the board's SPI modes and clocks; fault pins are inputs and enables start low before any valve code runs; the enable path drops both banks without SPI, latches, refuses energising and re-arms; shutdown ends in SafeHold; fail-closed bring-up with either chip absent, the bus down or TLE92466ED critical-fault replies, retry; MAX22200 VDR duty in percent and CDR / VDR reconfiguration of one channel; MAX22200 and TLE92466ED fault reporting; shutdown turns every channel off |
| `test_adc_manager` | Every pin-map channel registers; each ADS7952 channel reads its own input; the VBUS divider; name and enum lookups agree; failed reads are recorded; fail-closed with the ADS7952 absent, the pressure bus down or the MCU ADC down |
| `test_temperature_manager` | All three sensors; the NTC through its pin-map channel; the MCP9700 through the ADS7952; degraded mode without the ADC or the die sensor; failure with no sensor |
| `test_nvs_manager` | The three namespaces and their binding slots; isolation; the 15-character key limit; fail-closed opening |
| `test_flux_hal` | Init order and the required set; `CollectDiagnostics()` for a healthy board, a missing valve driver, a missing valve bus and a degraded temperature manager; system calls through the binding |

```bash
cmake -S tests/host -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
# with UndefinedBehaviorSanitizer:
cmake -S tests/host -B build/host-ubsan -G Ninja -DHF_HAL_SANITIZERS=undefined
```

AddressSanitizer is not used yet: hf-core's MAX22200 SPI adapter copies buffers with aligned word accesses that touch
the bytes around them (see hf-core's `docs/host-build.md`).

## Run the checks locally

```bash
# Build one example (needs ESP-IDF, see Get started)
cd examples/esp32 && ./scripts/build_app.sh flux_api_test Release && cd ../..

# HAL boundary lint and the host build of the managers (no ESP-IDF needed)
tools/lint/lint_hal_boundary.sh
tools/lint/lint_hal_alloc.sh
cmake -S tests/host -B build/host -G Ninja && cmake --build build/host

# Format check
clang-format --style=file:_config/.clang-format --dry-run --Werror lib/managers/*.cpp lib/managers/*.h

# Docs: drift checks, then the strict site build (the pin map needs lib/core and hf-pincfg)
python3 -m pip install "git+https://github.com/hardfoc/hf-general-ci-tools@v1#subdirectory=packages/hardfoc-mkdocs-theme"
hardfoc-docs gen --check
hardfoc-docs figures --check
mkdocs build --strict

# Markdown and YAML lint
npx markdownlint-cli2 --config _config/.markdownlint.json "docs/**/*.md" README.md
yamllint -c _config/.yamllint .github/workflows mkdocs.yml
```

## When the docs checks fail

| Message | Fix |
| --- | --- |
| `generated region is stale` | The pin map, `app_config.yml` or a manager's `#include`s changed. Run `hardfoc-docs gen` and commit |
| `... is stale (run hardfoc-docs figures)` | A figure's source data changed. Run `hardfoc-docs figures`, look at the SVGs, commit |
| `contains a link ... not found` | A page links to a file that does not exist. Fix the link |
| `image(s) have no alt text` | Every image needs `alt` text |
