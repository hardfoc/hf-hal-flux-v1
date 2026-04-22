# TemperatureManager - Temperature Monitoring System

<div align="center">

![Component](https://img.shields.io/badge/component-TemperatureManager-blue.svg)
![Thread Safe](https://img.shields.io/badge/thread--safe-yes-green.svg)
![Hardware](https://img.shields.io/badge/hardware-ESP32--S3%20%7C%20NTC-orange.svg)

**Multi-sensor temperature monitoring for the HardFOC Flux platform**

</div>

## 📋 Overview

The `TemperatureManager` is a singleton that provides unified temperature readings from the ESP32 internal sensor and an external 10 kΩ NTC thermistor. Both sensors are accessed through a consistent `BaseTemperature` interface.

### Key Features

- **🌡️ ESP32 Internal Sensor**: On-die temperature for thermal protection
- **🔌 NTC Thermistor**: 10 kΩ NTC for ambient/board temperature
- **🏷️ String-Based API**: Access sensors by name or functional enum
- **📈 Trend Analysis**: Temperature history and trend monitoring
- **🚨 Safety Alerts**: Over-temperature protection thresholds
- **🔒 Thread-Safe**: Concurrent access from multiple RTOS tasks

## 🏗️ Architecture

```
┌──────────────────────────────────┐
│       TemperatureManager          │  ← Singleton
├──────────────────────────────────┤
│  std::array<TempEntry, kMax>      │  ← Fixed-size sensor registry
│  BaseTemperature* esp32_temp_     │  ← Non-owning typed pointer
│  BaseTemperature* ntc_temp_       │  ← Non-owning typed pointer
├──────────────────────────────────┤
│  EspTemperature  │  NtcTempHandler │  ← Hardware drivers
│  (void* storage) │  (void* storage)│  ← Opaque owning storage
└──────────────────────────────────┘
```

## 📡 Temperature Sources

| Source | Type | Range | Use Case |
|--------|------|-------|----------|
| **ESP32 Internal** | On-die | -10°C to 80°C | MCU thermal protection |
| **NTC Thermistor** | 10 kΩ NTC | -40°C to 125°C | Board/ambient monitoring |

## 🔧 API Reference

### Initialization

```cpp
auto& temp = TemperatureManager::GetInstance();
bool ok = temp.EnsureInitialized();
```

### Reading Temperatures

```cpp
// By name
float board_temp = temp.ReadTemperature("TEMP_NTC");
float mcu_temp = temp.ReadTemperature("TEMP_ESP32_INTERNAL");

// By functional enum
float t = temp.ReadTemperature(HfFunctionalTempSensor::kNtc);
```

### Diagnostics

```cpp
auto diag = temp.GetSystemDiagnostics();
temp.DumpStatistics();
```

## 🚨 Error Codes

### TemperatureError Reference

| Code | Value | Description |
|------|-------|-------------|
| `SUCCESS` | 0 | Operation completed successfully |
| `NOT_INITIALIZED` | 1 | Manager not yet initialised |
| `INITIALIZATION_FAILED` | 2 | Sensor initialisation failed |
| `SENSOR_NOT_FOUND` | 3 | Named sensor does not exist in registry |
| `SENSOR_NOT_AVAILABLE` | 4 | Sensor exists but is unavailable |
| `READ_FAILED` | 5 | Driver-level read returned an error |
| `INVALID_PARAMETER` | 6 | Null pointer or out-of-range argument |
| `NO_SENSORS_AVAILABLE` | 7 | No sensors were registered at init |
| `ALLOCATION_FAILED` | 8 | Heap allocation for a sensor driver failed |
| `DEPENDENCY_NOT_READY` | 9 | Required dependency not initialised |
| `MUTEX_LOCK_FAILED` | 10 | RTOS mutex acquire timed out |

## ⚠️ Notes

- Sensor drivers are allocated once at init via `new (std::nothrow)` into `void*` storage — singleton lifetime
- `BaseTemperature*` typed pointers are non-owning aliases into the opaque storage
- The NTC thermistor uses `NtcTemperatureHandler` which reads through `AdcManager`
- No `Deinitialize()` method — sensors are active for the lifetime of the application
