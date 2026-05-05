# Flux — managers and hf-core handlers

Managers live in **`lib/managers/`**. Device logic and reusable handlers live under **`lib/core/`** (hf-core submodule).

| `Flux` member | Manager (HAL) | Primary `hf-core` / driver touchpoints |
|----------------|---------------|----------------------------------------|
| `nvs` | [`NvsManager`](../../lib/managers/NvsManager.h) | Board NVS helpers |
| `comms` | [`CommChannelsManager`](../../lib/managers/CommChannelsManager.h) | `BaseSpi`, CAN, UART |
| `gpio` | [`GpioManager`](../../lib/managers/GpioManager.h) | `BaseGpio` |
| `valves` | [`ValveManager`](../../lib/managers/ValveManager.h) | MAX22200 / TLE92466ED handlers under `lib/core/handlers/` and drivers under `lib/core/hf-core-drivers/` |
| `adc` | [`AdcManager`](../../lib/managers/AdcManager.h) | `BaseAdc` / ESP32 ADC |
| `leds` | [`LedManager`](../../lib/managers/LedManager.h) | WS2812 path |
| `temp` | [`TemperatureManager`](../../lib/managers/TemperatureManager.h) | NTC + ADC / internal sensor |

The **Flux V1** façade does not expose `EncoderManager`. For encoders / **AS5047U**, use handler sources under `lib/core/handlers/` and [hf-core Pages](https://hardfoc.github.io/hf-core/).

Generic definitions: [manager pattern](../hf-development-handbook/process/manager-pattern.md), [handler pattern](../hf-development-handbook/process/handler-pattern.md).
