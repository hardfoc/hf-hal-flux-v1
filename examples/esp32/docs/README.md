# Flux ESP32 examples — full index

Example-specific documentation: each **`APP_TYPE`** from [`../app_config.yml`](../app_config.yml), its entry source file(s), and CI. **HAL architecture** is under **[`docs/hal/`](../../../docs/hal/README.md)**.

## Build reminder

```bash
cd examples/esp32
./scripts/build_app.sh list
./scripts/build_app.sh flux_api_test Release
```

## Apps

| `APP_TYPE` | Source(s) | Category | CI | Notes |
|------------|-----------|----------|----|--------|
| `flux_api_test` | [`main/flux_api_test.cpp`](../main/flux_api_test.cpp) | system | yes | Broad `Flux` integration |
| `flux_nvs_example` | [`main/flux_nvs_example.cpp`](../main/flux_nvs_example.cpp) | example | yes | NVS namespaces, boot counter, blob/string |
| `valve_cycling_test` | [`main/valve_cycling_test.cpp`](../main/valve_cycling_test.cpp) | component | yes | Valve cycling |
| `gpio_adc_test` | [`main/gpio_adc_test.cpp`](../main/gpio_adc_test.cpp) | component | yes | GPIO + ADC |
| `led_temp_test` | [`main/led_temp_test.cpp`](../main/led_temp_test.cpp) | component | yes | LED + temperature |
| `adc_manager_example` | [`main/AdcManagerExample.cpp`](../main/AdcManagerExample.cpp) | example | no | Focused ADC demo |
| `comm_channels_example` | [`main/CommChannelsManagerExample.cpp`](../main/CommChannelsManagerExample.cpp) | example | no | Comms demo |
| `flux_api_example` | [`main/FluxApiExample.cpp`](../main/FluxApiExample.cpp) | example | no | Flux API demo |
| `gpio_manager_example` | [`main/GpioManagerExample.cpp`](../main/GpioManagerExample.cpp) | example | no | GPIO manager demo |
| `led_manager_example` | [`main/LedManagerExample.cpp`](../main/LedManagerExample.cpp) | example | no | LED manager demo |
| `temperature_example` | [`main/TemperatureManagerExample.cpp`](../main/TemperatureManagerExample.cpp) | example | no | Temperature demo |
| `valve_manager_example` | [`main/ValveManagerExample.cpp`](../main/ValveManagerExample.cpp) | example | no | Valve manager demo |
| `ads7952_example` | [`main/Ads7952Example.cpp`](../main/Ads7952Example.cpp) | example | no | ADS7952 demo |
| `alicat_basis2_example` | [`main/AlicatBasis2Example.cpp`](../main/AlicatBasis2Example.cpp) | example | no | Alicat Basis 2 demo |
| `manifold_valves_pressure_control` | [`main/manifold_valves_pressure_control/manifold_valves_pressure_control.cpp`](../main/manifold_valves_pressure_control/manifold_valves_pressure_control.cpp) (+ linked `.cpp` in `app_config.yml`) | example | no | Multi-file app; see [`main/manifold_valves_pressure_control/README.md`](../main/manifold_valves_pressure_control/README.md) |

## Tie-back

- **[`docs/hal/architecture.md`](../../../docs/hal/architecture.md)** — fatal vs non-fatal init steps on Flux  
- **[`docs/hal/managers-and-handlers.md`](../../../docs/hal/managers-and-handlers.md)** — jump to manager headers  
