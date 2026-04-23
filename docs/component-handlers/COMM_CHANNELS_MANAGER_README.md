# CommChannelsManager - Communication Channels Management System

<div align="center">

![Component](https://img.shields.io/badge/component-CommChannelsManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-ESP32--S3-orange.svg)

**Unified communication bus management for the HardFOC Flux platform**

</div>

## 📋 Overview

The `CommChannelsManager` is a singleton that initializes and manages communication buses (SPI, CAN/TWAI, and lazy-acquired UART ports) on the Flux board. It provides typed access to bus instances for use by other managers and driver handlers.

### Key Features

- **SPI bus**: SPI2 with MAX22200 and TLE92466ED device connections (plus optional SPI3 / ADS7952)
- **CAN/TWAI**: Controller Area Network bus for external controllers
- **UART**: `AcquireUart()` lazy pool for BASIS-2, FDO2, etc. (no I²C on Flux V1)
- **USB Serial/JTAG (opt-in)**: `AcquireUsbSerialJtag()` lazy singleton for the ESP32-S3 native-USB CDC-ACM console — gated by the CMake flag `HF_CORE_ENABLE_USB_SERIAL_JTAG` (default **OFF**). Returns `nullptr` when the flag is off so app code compiles unchanged.
- **🏷️ CommError Enum**: Typed error codes for all communication failures
- **🔒 Thread-Safe**: Bus access guarded by RTOS mutexes

## 🏗️ Architecture

```
┌─────────────────────────────────┐
│       CommChannelsManager       │  ← Singleton
├─────────────────────────────────┤
│  EspSpiBus*   spi_bus_          │  ← SPI2 (MAX22200 + TLE92466ED)
│  EspSpiBus*   spi3_bus_         │  ← SPI3 (ADS7952 when enabled)
│  EspCan*      can_bus_          │  ← CAN/TWAI
│  EspUart[]    uarts_[]          │  ← lazy UART pool (AcquireUart)
│  BaseUsbSerialJtag* usj_        │  ← opt-in (HF_CORE_ENABLE_USB_SERIAL_JTAG)
├─────────────────────────────────┤
│  ESP32 SPI / TWAI / UART / USJ  │
└─────────────────────────────────┘
```

## 📡 Communication Interfaces

| Bus | Driver | Connected Devices |
|-----|--------|-------------------|
| **SPI2** | `EspSpiBus` | MAX22200 (solenoid driver), TLE92466ED (proportional driver) |
| **SPI3** | `EspSpiBus` | ADS7952 (when `HARDFOC_ADS7952_SUPPORT` is defined) |
| **CAN** | `EspCan` | External CAN devices (e.g. compressor controller) |
| **UART** | `EspUart` via `AcquireUart` | Serial sensors (BASIS-2, FDO2, …) |
| **USB Serial/JTAG** | `EspUsbSerialJtag` via `AcquireUsbSerialJtag` | ESP32-S3 native-USB console (CDC-ACM + JTAG over one cable). **Opt-in** via `HF_CORE_ENABLE_USB_SERIAL_JTAG=ON`; `nullptr` otherwise. |

## 🔧 API Reference

### Initialization

```cpp
auto& comm = CommChannelsManager::GetInstance();
bool ok = comm.EnsureInitialized();
```

### Bus Access

```cpp
// SPI bus for valve driver communication
EspSpiBus* spi = comm.GetSpiBus();

// CAN bus
EspCan* can = comm.GetCanBus();

// USB Serial/JTAG console (ESP32-S3 native USB).
// Returns nullptr unless the build was configured with
// HF_CORE_ENABLE_USB_SERIAL_JTAG=ON in hf-hal-flux-v1/CMakeLists.txt.
BaseUsbSerialJtag* usj = comm.AcquireUsbSerialJtag();
if (usj && usj->EnsureInitialized()) {
    usj->WriteString("hello from USB Serial/JTAG\r\n");
}
```

### Build-time gating

The USB Serial/JTAG path is compiled out by default to keep the binary
small and to avoid pulling in `esp_driver_usb_serial_jtag` on boards that
don't use it. To enable it, flip the flag in
`hal/hf-hal-flux-v1/CMakeLists.txt`:

```cmake
set(HF_CORE_ENABLE_USB_SERIAL_JTAG ON CACHE BOOL "" FORCE)
```

This causes `hf_core_build_settings.cmake` to:

1. Add `esp_driver_usb_serial_jtag` to `HF_CORE_IDF_REQUIRES`.
2. Append `EspUsbSerialJtag.cpp` to `HF_CORE_INTERFACE_SOURCES`.
3. Define `HARDFOC_USB_SERIAL_JTAG_SUPPORT=1` for the build.

When the flag is OFF, `AcquireUsbSerialJtag()` simply returns `nullptr`
and the include of `EspUsbSerialJtag.h` inside `CommChannelsManager.cpp`
is `#ifdef`-guarded — application code that calls the accessor still
compiles and links cleanly, it just gets a null pointer back.

See also: [`BaseUsbSerialJtag`](../../lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/docs/api/BaseUsbSerialJtag.md)
and [`EspUsbSerialJtag`](../../lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/docs/esp_api/EspUsbSerialJtag.md).

### Error Handling

```cpp
// CommError enum for typed error handling
CommError err = comm.CreateSpiDevice(/* ... */);
if (err != CommError::SUCCESS) {
    // Handle error
}
```

## 🔢 CommError Enum

| Code | Value | Description |
|------|-------|-------------|
| `SUCCESS` | 0 | Operation completed successfully |
| `NOT_INITIALIZED` | 1 | Manager not yet initialised |
| `INITIALIZATION_FAILED` | 2 | Bus or device initialisation failed |
| `BUS_NOT_AVAILABLE` | 3 | Requested bus is not present or not ready |
| `DEVICE_NOT_FOUND` | 4 | Device index or id not in registry |
| `INVALID_BUS_INDEX` | 5 | Bus index out of range |
| `INVALID_DEVICE_INDEX` | 6 | Device index out of range |
| `DEVICE_ALREADY_EXISTS` | 7 | A device at that address already exists |
| `DEVICE_CREATION_FAILED` | 8 | Heap allocation or driver init for device failed |
| `COMMUNICATION_FAILED` | 9 | A bus-level transfer failed |
| `INVALID_PARAMETER` | 10 | Null pointer or out-of-range argument |
| `MUTEX_LOCK_FAILED` | 11 | RTOS mutex acquire timed out |

## ⚠️ Notes

- Bus instances are allocated once at `EnsureInitialized()` and live for singleton lifetime
- `GetSpiBus()` / `GetI2cBus()` / `GetCanBus()` return non-owning pointers — callers must not `delete` them
- Error-path cleanup uses explicit `delete` for partially-initialized buses
