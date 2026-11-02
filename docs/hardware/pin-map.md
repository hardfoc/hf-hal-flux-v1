# Pin map

Every pin and ADC channel the HAL knows about. The single source is the X-macro table in
[`hf_functional_pin_config_flux_v1.hpp`](../../lib/core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp)
(in the `hf-pincfg` submodule); the managers register their pins from it at start-up and these tables are generated
from it by `hardfoc-docs gen`, so the docs cannot drift from the firmware.

How to read the tables:

- **Function** is the `HfFunctionalGpioPin` / `HfFunctionalAdcChannel` enum value. Code that needs a pin uses it.
- **Name** is the string you pass to `GpioManager::Get("...")` or `AdcManager::ReadVoltage("...")`.
- **Category**: *core* pins are reserved by the system, *comm* pins belong to a bus and are configured by
  `CommChannelsManager`, *GPIO* pins are registered by `GpioManager` and can be driven by name.
- **Active low** pins (`_N` suffix, chip selects, reset) are inverted for you: `SetActive()` drives them low.

## Summary

<!-- generated: pins-summary -->
From
[`hf_functional_pin_config_flux_v1.hpp`](../../lib/core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp);
do not edit.

| Pin source | Functional pins | Of which: core | comm | GPIO |
| --- | ---: | ---: | ---: | ---: |
| ESP32-S3 GPIO | 27 | 3 | 14 | 10 |
| ADC channels | 14 | | | |
<!-- /generated -->

## ESP32-S3 GPIO

<!-- generated: pins-esp32 -->
From
[`hf_functional_pin_config_flux_v1.hpp`](../../lib/core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp);
do not edit.

| Function | Pin | Category | Name (for `GpioManager`) | Active | Pull | Max mA |
| --- | --- | --- | --- | --- | --- | ---: |
| `CORE_BOOT` | GPIO0 | Core | `CORE_BOOT` | high | none | - |
| `MAX22200_ENABLE` | GPIO2 | Gpio | `GPIO_MAX22200_ENABLE` | high | none | 40 |
| `WS2812_LED_DAT` | GPIO3 | Comm | `COMM_WS2812_LED_DAT` | high | none | 40 |
| `SPI2_CS_TLE92466` | GPIO4 | Comm | `COMM_SPI2_CS_TLE92466` | low | up | 40 |
| `TLE92466_EN` | GPIO5 | Gpio | `GPIO_TLE92466_EN` | high | none | 40 |
| `TLE92466_RESN` | GPIO6 | Gpio | `GPIO_TLE92466_RESN` | low | none | 40 |
| `TLE92466_DRV0` | GPIO7 | Gpio | `GPIO_TLE92466_DRV0` | high | none | 40 |
| `SPI3_CS_ADS7952_0` | GPIO10 | Comm | `COMM_SPI3_CS_ADS7952_0` | low | up | 40 |
| `SPI3_MOSI` | GPIO11 | Comm | `COMM_SPI3_MOSI` | high | none | 40 |
| `SPI3_SCK` | GPIO12 | Comm | `COMM_SPI3_SCK` | high | none | 40 |
| `SPI3_MISO` | GPIO13 | Comm | `COMM_SPI3_MISO` | high | up | 40 |
| `TLE92466_DRV1` | GPIO15 | Gpio | `GPIO_TLE92466_DRV1` | high | none | 40 |
| `TLE92466_FAULT_N` | GPIO16 | Gpio | `GPIO_TLE92466_FAULT_N` | low | up | 40 |
| `TWAI_TX` | GPIO17 | Comm | `COMM_TWAI_TX` | high | up | 40 |
| `TWAI_RX` | GPIO18 | Comm | `COMM_TWAI_RX` | high | up | 40 |
| `USB_D_N` | GPIO19 | Core | `CORE_USB_D_N` | high | none | - |
| `USB_D_P` | GPIO20 | Core | `CORE_USB_D_P` | high | none | - |
| `SPI2_MISO` | GPIO35 | Comm | `COMM_SPI2_MISO` | high | up | 40 |
| `SPI2_SCK` | GPIO36 | Comm | `COMM_SPI2_SCK` | high | none | 40 |
| `SPI2_MOSI` | GPIO37 | Comm | `COMM_SPI2_MOSI` | high | none | 40 |
| `SPI2_CS_MAX22200` | GPIO38 | Comm | `COMM_SPI2_CS_MAX22200` | low | up | 40 |
| `MAX22200_CMD` | GPIO39 | Gpio | `GPIO_MAX22200_CMD` | high | none | 40 |
| `MAX22200_TRIGA` | GPIO40 | Gpio | `GPIO_MAX22200_TRIGA` | high | none | 40 |
| `MAX22200_TRIGB` | GPIO41 | Gpio | `GPIO_MAX22200_TRIGB` | high | none | 40 |
| `MAX22200_FAULT_N` | GPIO42 | Gpio | `GPIO_MAX22200_FAULT_N` | low | up | 40 |
| `UART_FDO2_TX` | GPIO47 | Comm | `COMM_UART_FDO2_TX` | high | up | 40 |
| `UART_FDO2_RX` | GPIO48 | Comm | `COMM_UART_FDO2_RX` | high | up | 40 |
<!-- /generated -->

## ADC channels

The two ESP32 channels use ADC1 with 12 dB attenuation; the reading of `ESP32_VBUS_MONITOR` is multiplied by its
divider before `AdcManager` returns it. The ADS7952 channels are only registered when the build defines
`HARDFOC_ADS7952_SUPPORT` (the HAL's `CMakeLists.txt` does).

<!-- generated: pins-adc -->
From
[`hf_functional_pin_config_flux_v1.hpp`](../../lib/core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp);
do not edit.

| Channel | Name (for `AdcManager`) | Source | Ch | Bits | Full scale | Description |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `ESP32_NTC_THERMISTOR` | `ADC_NTC_THERMISTOR` | ESP32-S3 ADC1 | 7 | 12 | 3300 mV | NTC Thermistor Temperature Input |
| `ESP32_VBUS_MONITOR` | `ADC_VBUS_MONITOR` | ESP32-S3 ADC1 | 8 | 12 | 3300 mV (divider 11) | Bus Voltage Monitor (with divider) |
| `ADS7952_0_CH0` | `ADC_ADS7952_0_CH0` | ADS7952 #0 | 0 | 12 | 2500 mV | ADS7952 Dev0 CH0 |
| `ADS7952_0_CH1` | `ADC_ADS7952_0_CH1` | ADS7952 #0 | 1 | 12 | 2500 mV | ADS7952 Dev0 CH1 |
| `ADS7952_0_CH2` | `ADC_ADS7952_0_CH2` | ADS7952 #0 | 2 | 12 | 2500 mV | ADS7952 Dev0 CH2 |
| `ADS7952_0_CH3` | `ADC_ADS7952_0_CH3` | ADS7952 #0 | 3 | 12 | 2500 mV | ADS7952 Dev0 CH3 |
| `ADS7952_0_CH4` | `ADC_ADS7952_0_CH4` | ADS7952 #0 | 4 | 12 | 2500 mV | ADS7952 Dev0 CH4 |
| `ADS7952_0_CH5` | `ADC_ADS7952_0_CH5` | ADS7952 #0 | 5 | 12 | 2500 mV | ADS7952 Dev0 CH5 |
| `ADS7952_0_CH6` | `ADC_ADS7952_0_CH6` | ADS7952 #0 | 6 | 12 | 2500 mV | ADS7952 Dev0 CH6 |
| `ADS7952_0_CH7` | `ADC_ADS7952_0_CH7` | ADS7952 #0 | 7 | 12 | 2500 mV | ADS7952 Dev0 CH7 |
| `ADS7952_0_CH8` | `ADC_ADS7952_0_CH8` | ADS7952 #0 | 8 | 12 | 2500 mV | ADS7952 Dev0 CH8 |
| `ADS7952_0_CH9` | `ADC_ADS7952_0_CH9` | ADS7952 #0 | 9 | 12 | 2500 mV | ADS7952 Dev0 CH9 |
| `ADS7952_0_CH10` | `ADC_ADS7952_0_CH10` | ADS7952 #0 | 10 | 12 | 2500 mV | ADS7952 Dev0 CH10 |
| `ADS7952_0_CH11` | `ADC_ADS7952_0_CH11` | ADS7952 #0 | 11 | 12 | 2500 mV | ADS7952 Dev0 CH11 |
<!-- /generated -->

## Changing a pin

1. Edit the row in the pincfg header (in the `hf-pincfg` repository, then advance the submodule here).
2. Run `hardfoc-docs gen` and `hardfoc-docs figures` to refresh these tables and the board figure.
3. Commit both. CI fails the pull request if the docs and the header disagree.
