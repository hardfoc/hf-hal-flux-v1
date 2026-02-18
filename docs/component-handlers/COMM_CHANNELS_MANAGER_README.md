# CommChannelsManager - Communication Channels Management System

<div align="center">

![Component](https://img.shields.io/badge/component-CommChannelsManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-ESP32--C6-orange.svg)

**Unified communication bus management for the HardFOC Flux platform**

</div>

## 📋 Overview

The `CommChannelsManager` is a singleton that initializes and manages all communication buses (SPI, I2C, CAN/TWAI) on the Flux board. It provides typed access to bus instances for use by other managers and driver handlers.

### Key Features

- **🔌 SPI Bus**: SPI2 with MAX22200 and TLE92466ED device connections
- **📡 I2C Bus**: I2C master for peripheral expansion
- **🚌 CAN/TWAI**: Controller Area Network bus for inter-board communication
- **🏷️ CommError Enum**: Typed error codes for all communication failures
- **🔒 Thread-Safe**: Bus access guarded by RTOS mutexes

## 🏗️ Architecture

```
┌─────────────────────────────────┐
│       CommChannelsManager        │  ← Singleton
├─────────────────────────────────┤
│  EspSpiBus*   spi_bus_           │  ← SPI2 (MAX22200 + TLE92466ED)
│  EspI2cBus*   i2c_bus_           │  ← I2C master
│  EspCan*      can_bus_           │  ← CAN/TWAI
├─────────────────────────────────┤
│  ESP32-C6 SPI / I2C / TWAI HW   │
└─────────────────────────────────┘
```

## 📡 Communication Interfaces

| Bus | Driver | Connected Devices |
|-----|--------|-------------------|
| **SPI2** | `EspSpiBus` | MAX22200 (solenoid driver), TLE92466ED (proportional driver) |
| **I2C** | `EspI2cBus` | Peripheral expansion (placeholder) |
| **CAN** | `EspCan` | Inter-board CAN bus communication |

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

// I2C bus
EspI2cBus* i2c = comm.GetI2cBus();

// CAN bus
EspCan* can = comm.GetCanBus();
```

### Error Handling

```cpp
// CommError enum for typed error handling
CommError err = comm.SendMessage(/* ... */);
if (err != CommError::kOk) {
    const char* desc = CommErrorToString(err);
    // Handle: kBusNotInitialized, kTransmitFailed, kTimeout, etc.
}
```

## 🔢 CommError Enum

| Value | Meaning |
|-------|---------|
| `kOk` | Success |
| `kBusNotInitialized` | Bus not yet initialized |
| `kInvalidParameter` | Invalid argument |
| `kTransmitFailed` | TX failure |
| `kReceiveFailed` | RX failure |
| `kTimeout` | Operation timed out |
| `kBusBusy` | Bus in use by another task |
| `kNotSupported` | Operation not supported |
| `kBufferOverflow` | Buffer size exceeded |
| `kProtocolError` | Protocol-level error |
| `kDeviceNotFound` | Target device not responding |
| `kUnknownError` | Catch-all |

## ⚠️ Notes

- Bus instances are allocated once at `EnsureInitialized()` and live for singleton lifetime
- `GetSpiBus()` / `GetI2cBus()` / `GetCanBus()` return non-owning pointers — callers must not `delete` them
- Error-path cleanup uses explicit `delete` for partially-initialized buses
