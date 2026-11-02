# CommChannelsManager

Owns the board's buses: the valve SPI bus (MAX22200 and TLE92466ED), the ADC SPI bus (ADS7952), the CAN controller,
and up to three UARTs created on demand. Other managers borrow devices from it; application code rarely needs it
except for CAN and UARTs. Source: [`CommChannelsManager.h`](../../lib/managers/CommChannelsManager.h).

## What it sets up

| Bus | Pins (pin map names) | Devices | Settings |
| --- | --- | --- | --- |
| Valve SPI (ESP-IDF `SPI3_HOST`) | `SPI2_SCK`, `SPI2_MOSI`, `SPI2_MISO` | `MAX22200_SOLENOID_DRIVER` (CS `SPI2_CS_MAX22200`), `TLE92466_LOWSIDE_DRIVER` (CS `SPI2_CS_TLE92466`) | Mode 0 and mode 1, both 1 MHz |
| ADC SPI (ESP-IDF `SPI2_HOST`) | `SPI3_SCK`, `SPI3_MOSI`, `SPI3_MISO` | `ADS7952_ADC_0` (CS `SPI3_CS_ADS7952_0`) | Mode 0, 4 MHz; only with `HARDFOC_ADS7952_SUPPORT` |
| CAN (TWAI) | `TWAI_TX`, `TWAI_RX` | - | 500 kbit/s |
| UART 0-2 | chosen by the caller | e.g. the FDO2 probe on `UART_FDO2_TX` / `UART_FDO2_RX` | Created by `AcquireUart()` |

GPIO numbers are on the [pin map](../hardware/pin-map.md). There is no I2C bus on Flux V1.

## API

| Call | Returns | Notes |
| --- | --- | --- |
| `GetSpiDevice(SpiDeviceId)` | `BaseSpi*` | `nullptr` before initialisation or if the device failed to attach |
| `GetCanBus()` | `BaseCan*` | `nullptr` if CAN failed to start |
| `AcquireUart(const UartAcquireConfig&)` | `BaseUart*` | First call for a port creates and caches it; later calls return the cached port and ignore the new config. `rts_pin >= 0` enables RS-485 half-duplex |
| `GetUart(port)` | `BaseUart*` | The cached port, or `nullptr` |
| `AcquireUsbSerialJtag(cfg)`, `GetUsbSerialJtag()` | `BaseUsbSerialJtag*` | The USB serial / JTAG peripheral; returns `nullptr` because `HF_CORE_ENABLE_USB_SERIAL_JTAG` is off in this HAL's `CMakeLists.txt` |
| `GetI2cBus()` | `BaseI2c*` | Always `nullptr` on Flux V1 |
| `GetLastError()`, `GetSystemDiagnostics(CommSystemDiagnostics&)`, `DumpStatistics()` | | See below |

## Example: open a UART for a sensor

```cpp
#include "api/Flux.h"
#include "managers/CommChannelsManager.h"

CommChannelsManager::UartAcquireConfig cfg{};
cfg.port_number = 1;
cfg.tx_pin = 47;      // UART_FDO2_TX on the pin map
cfg.rx_pin = 48;      // UART_FDO2_RX
cfg.rts_pin = -1;     // plain UART (no RS-485 direction pin)
cfg.cts_pin = -1;
cfg.baud_bps = 19200;
cfg.tx_buffer_size = 256;
cfg.rx_buffer_size = 256;

BaseUart* uart = Flux::GetInstance().comms.AcquireUart(cfg);
if (uart == nullptr) {
    // port out of range or the UART failed to start: see the log
}
```

Read the pin numbers from the pin map in real code (`GetGpioMapping(HfFunctionalGpioPin::UART_FDO2_TX)`) rather
than writing them down.

## Diagnostics

`GetSystemDiagnostics()` fills `CommSystemDiagnostics`: `spi_bus_valid`, `can_bus_valid`, `spi_device_count` and the
last `CommError`. `i2c_bus_valid` is always false. `DumpStatistics()` logs the bus states.

## Pitfalls

- **Names versus hosts.** `SPI2_*` pins are the ESP-IDF `SPI3_HOST`, `SPI3_*` pins are `SPI2_HOST`
  (see [Hardware](../hardware/index.md)).
- **Sharing a device.** The valve drivers already use their SPI devices through `ValveManager`. Talking to them
  directly through `GetSpiDevice()` bypasses `ValveManager`'s lock and channel state.
- **UART port 0** is normally the ESP-IDF console. Pick a free port.
- **First `AcquireUart()` wins.** A second call for the same port with a different baud rate silently gets the
  first configuration (the header comment promises a warning; the code does not log one).
