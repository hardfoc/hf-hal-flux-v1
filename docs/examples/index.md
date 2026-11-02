# Examples

Every app you can build from [`examples/esp32`](../../examples/esp32). Each app is one entry `.cpp` file in
`examples/esp32/main/`, selected by its name (`APP_TYPE`); the build defines `EXAMPLE_TYPE_<name>=1` for it.

## Run one

```bash
cd examples/esp32
./scripts/build_app.sh <app> Release
./scripts/flash_app.sh flash_monitor <app> Release
```

`./scripts/build_app.sh list` prints the names. See [Get started](../quickstart.md) for the toolchain.

!!! tip "Which one first"
    `flux_api_test` initialises everything and checks each manager, so it is the quickest way to see whether a board
    is healthy. Then pick the documentation example for the manager you care about.

## Catalogue

<!-- generated: examples-catalogue -->
From [`examples/esp32/app_config.yml`](../../examples/esp32/app_config.yml)
and each source file's `@brief`; do not edit.

15 apps, 5 built by CI on every pull request. Target `esp32s3`, ESP-IDF release/v5.5.

### Start here

| App (`APP_TYPE`) | What it does | CI | Source |
| --- | --- | :---: | --- |
| `flux_api_test` | Comprehensive Flux HAL API test suite for ESP32 (noexcept) | yes | [`flux_api_test.cpp`](../../examples/esp32/main/flux_api_test.cpp) |

### Manager tests

| App (`APP_TYPE`) | What it does | CI | Source |
| --- | --- | :---: | --- |
| `valve_cycling_test` | Valve manager focused cycling test for ESP32 | yes | [`valve_cycling_test.cpp`](../../examples/esp32/main/valve_cycling_test.cpp) |
| `gpio_adc_test` | GPIO & ADC focused test for Flux ESP32 | yes | [`gpio_adc_test.cpp`](../../examples/esp32/main/gpio_adc_test.cpp) |
| `led_temp_test` | LED & Temperature focused test for Flux ESP32 | yes | [`led_temp_test.cpp`](../../examples/esp32/main/led_temp_test.cpp) |

### Documentation examples

| App (`APP_TYPE`) | What it does | CI | Source |
| --- | --- | :---: | --- |
| `flux_nvs_example` | Flux NvsManager — App/Cal/Config namespaces, boot counter, blob, string | yes | [`flux_nvs_example.cpp`](../../examples/esp32/main/flux_nvs_example.cpp) |
| `adc_manager_example` | Documentation example: ADC reading on Flux V1. | - | [`AdcManagerExample.cpp`](../../examples/esp32/main/AdcManagerExample.cpp) |
| `comm_channels_example` | Documentation example: Communication channels usage on Flux V1. | - | [`CommChannelsManagerExample.cpp`](../../examples/esp32/main/CommChannelsManagerExample.cpp) |
| `flux_api_example` | Comprehensive documentation example: Full Flux API on Flux V1. | - | [`FluxApiExample.cpp`](../../examples/esp32/main/FluxApiExample.cpp) |
| `gpio_manager_example` | Documentation example: GPIO management on Flux V1. | - | [`GpioManagerExample.cpp`](../../examples/esp32/main/GpioManagerExample.cpp) |
| `led_manager_example` | Documentation example: WS2812 LED control on Flux V1. | - | [`LedManagerExample.cpp`](../../examples/esp32/main/LedManagerExample.cpp) |
| `temperature_example` | Documentation example: Temperature monitoring on Flux V1. | - | [`TemperatureManagerExample.cpp`](../../examples/esp32/main/TemperatureManagerExample.cpp) |
| `valve_manager_example` | Documentation example: Unified valve control on Flux V1. | - | [`ValveManagerExample.cpp`](../../examples/esp32/main/ValveManagerExample.cpp) |
| `ads7952_example` | Documentation example: ADS7952 12-channel ADC on Flux V1. | - | [`Ads7952Example.cpp`](../../examples/esp32/main/Ads7952Example.cpp) |
| `alicat_basis2_example` | Comprehensive Flux V1 example exercising the Alicat BASIS-2 driver and `AlicatBasis2Handler` over an ESP32-S3 UART configured for RS-485 half-duplex. | - | [`AlicatBasis2Example.cpp`](../../examples/esp32/main/AlicatBasis2Example.cpp) |

### Bench applications

| App (`APP_TYPE`) | What it does | CI | Source |
| --- | --- | :---: | --- |
| `manifold_valves_pressure_control` | Manifold test bench: pressure sensor calibration and high-rate monitoring. | - | [`manifold_valves_pressure_control.cpp`](../../examples/esp32/main/manifold_valves_pressure_control/manifold_valves_pressure_control.cpp) |
<!-- /generated -->

## Adding an app

1. Add the `.cpp` file under `examples/esp32/main/` with a Doxygen `@brief` on its first comment block (the
   catalogue reads it).
2. Add an entry under `apps:` in `app_config.yml` (`source_file`, `category`, `ci_enabled`).
3. Run `hardfoc-docs gen` to refresh this page, and commit both.
