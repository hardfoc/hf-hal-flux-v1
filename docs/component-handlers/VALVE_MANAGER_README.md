# ValveManager - Valve Control System

<div align="center">

![Component](https://img.shields.io/badge/component-ValveManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-MAX22200%20%7C%20TLE92466ED-orange.svg)

**Multi-channel valve control for the HardFOC Flux platform**

</div>

## 📋 Overview

The `ValveManager` is a singleton that manages 14 valve channels across two driver ICs: 8 solenoid channels via MAX22200 and 6 proportional channels via TLE92466ED. It provides a unified API for all valve types with compile-time channel mapping.

### Key Features

- **🔧 14 Channels**: 8 solenoid (MAX22200) + 6 proportional (TLE92466ED)
- **⚡ Compile-Time Mapping**: `kValveChannelMap[]` for O(1) channel resolution
- **🏷️ Unified Error Codes**: `hf_valve_err_t` with driver-specific error mapping
- **🎛️ Proportional Control**: PWM duty cycle for TLE92466ED channels
- **🚨 Diagnostics**: Per-channel fault detection and status reporting
- **🔒 Thread-Safe**: Concurrent access via RTOS mutex

## 🏗️ Architecture

```
┌────────────────────────────────────────────┐
│              ValveManager                   │  ← Singleton
├────────────────────────────────────────────┤
│  kValveChannelMap[14]                       │  ← Compile-time channel map
│  Max22200Handler*  max22200_                │  ← 8 solenoid channels
│  Tle92466edHandler* tle92466_               │  ← 6 proportional channels
├────────────────────────────────────────────┤
│  MAX22200 (SPI)    │  TLE92466ED (SPI)      │  ← Hardware drivers
└────────────────────────────────────────────┘
```

## 📡 Valve Channels

| Channel Range | Driver | Type | Control |
|---------------|--------|------|---------|
| **0–7** | MAX22200 | Solenoid | On/Off, current limit |
| **8–13** | TLE92466ED | Proportional | PWM duty cycle (0–100%) |

## 🔧 API Reference

### Initialization

```cpp
auto& valves = ValveManager::GetInstance();
bool ok = valves.EnsureInitialized();  // Returns true on success
```

### Solenoid Control (Channels 0–7)

```cpp
// Turn on/off
valves.SetChannel(0, true);   // Open solenoid 0
valves.SetChannel(3, false);  // Close solenoid 3

// Batch control
valves.SetAllChannels(0xFF);  // Open all solenoids
```

### Proportional Control (Channels 8–13)

```cpp
// Set duty cycle (0.0 – 1.0)
valves.SetProportional(8, 0.5f);   // 50% open
valves.SetProportional(10, 0.75f); // 75% open
```

### Status & Diagnostics

```cpp
auto status = valves.GetChannelStatus(0);
auto diag = valves.GetSystemDiagnostics();
valves.DumpStatistics();
```

### Error Handling

```cpp
hf_valve_err_t err = valves.SetChannel(0, true);
if (err != hf_valve_err_t::VALVE_SUCCESS) {
    // See error table below
}
```

## 🔢 Error Codes (`hf_valve_err_t`)

| Category | Code | Value | Description |
|----------|------|-------|-------------|
| **General** | `VALVE_SUCCESS` | 0 | Success |
| | `VALVE_ERR_NOT_INITIALIZED` | 1 | Manager not initialized |
| | `VALVE_ERR_DRIVER_NOT_AVAILABLE` | 2 | Driver not available |
| | `VALVE_ERR_INVALID_CHANNEL` | 3 | Channel index out of range |
| | `VALVE_ERR_CHANNEL_TYPE_MISMATCH` | 4 | Channel type mismatch |
| | `VALVE_ERR_INVALID_PARAMETER` | 5 | Invalid parameter |
| | `VALVE_ERR_NULL_POINTER` | 6 | Null pointer |
| **SPI** | `VALVE_ERR_SPI_BUS_ERROR` | 10 | SPI bus error |
| | `VALVE_ERR_SPI_TIMEOUT` | 11 | SPI timeout |
| | `VALVE_ERR_SPI_TRANSFER_FAILED` | 12 | SPI transfer failed |
| | `VALVE_ERR_SPI_CRC_ERROR` | 13 | SPI CRC error |
| | `VALVE_ERR_SPI_FRAME_ERROR` | 14 | SPI frame error |
| **Driver** | `VALVE_ERR_HARDWARE_FAULT` | 20 | Hardware fault |
| | `VALVE_ERR_DEVICE_NOT_RESPONDING` | 21 | Device not responding |
| | `VALVE_ERR_WRONG_DEVICE_ID` | 22 | Wrong device ID |
| | `VALVE_ERR_REGISTER_ERROR` | 23 | Register error |
| | `VALVE_ERR_CONFIGURATION_ERROR` | 24 | Configuration error |
| | `VALVE_ERR_WRONG_MODE` | 25 | Wrong mode |
| | `VALVE_ERR_WRITE_TO_READ_ONLY` | 26 | Write to read-only register |
| | `VALVE_ERR_TIMEOUT` | 27 | Timeout |
| **Manager** | `VALVE_ERR_MUTEX_TIMEOUT` | 30 | Mutex timeout |
| | `VALVE_ERR_DEPENDENCY_NOT_READY` | 31 | Dependency not ready |
| | `VALVE_ERR_UNKNOWN` | 255 | Unknown error |

## ⚠️ Notes

- Handler memory is allocated once via `new (std::nothrow)` into `void*` opaque storage — singleton lifetime
- `Max22200Handler*` and `Tle92466edHandler*` are non-owning typed aliases into storage
- Both drivers communicate over the shared SPI2 bus managed by `CommChannelsManager`
- Channel mapping is resolved at compile time — no runtime lookup overhead
