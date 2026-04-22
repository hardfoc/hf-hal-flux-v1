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
├─────────────────────────────────┤
│  ESP32 SPI / TWAI / UART HW     │
└─────────────────────────────────┘
```

## 📡 Communication Interfaces

| Bus | Driver | Connected Devices |
|-----|--------|-------------------|
| **SPI2** | `EspSpiBus` | MAX22200 (solenoid driver), TLE92466ED (proportional driver) |
| **SPI3** | `EspSpiBus` | ADS7952 (when `HARDFOC_ADS7952_SUPPORT` is defined) |
| **CAN** | `EspCan` | External CAN devices (e.g. compressor controller) |
| **UART** | `EspUart` via `AcquireUart` | Serial sensors (BASIS-2, FDO2, …) |

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
```

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
