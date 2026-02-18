# As5047uHandler - Position Encoder Driver Handler

<div align="center">

![Driver](https://img.shields.io/badge/driver-As5047uHandler-blue.svg)
![Hardware](https://img.shields.io/badge/hardware-AS5047U-orange.svg)
![Interface](https://img.shields.io/badge/interface-SPI-green.svg)

**Unified handler for AS5047U magnetic rotary position sensor with SPI integration**

</div>

## 📋 Overview

The `As5047uHandler` is a unified handler for AS5047U magnetic rotary position sensor that provides a modern, comprehensive interface for high-precision angle measurement and velocity calculation. It supports SPI communication, offers advanced features like Dynamic Angle Error Compensation (DAEC), and provides comprehensive diagnostics and error handling.

### ✨ Key Features

- **🎯 High Precision**: 14-bit absolute angle measurement (0-16383 counts per revolution)
- **⚡ Velocity Measurement**: Multiple unit conversions (rad/s, deg/s, RPM)
- **🔧 Dynamic Angle Error Compensation**: DAEC for improved accuracy
- **📡 SPI Interface**: Configurable frame formats (16/24/32-bit)
- **🛡️ Thread-Safe**: Concurrent access from multiple tasks
- **🏥 Comprehensive Diagnostics**: Magnetic field monitoring and error detection
- **⚙️ Advanced Configuration**: OTP programming and interface outputs
- **🔍 Health Monitoring**: AGC monitoring and calibration status

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                   As5047uHandler                               │
├─────────────────────────────────────────────────────────────────┤
│  Angle Measurement │ 14-bit absolute position sensing          │
├─────────────────────────────────────────────────────────────────┤
│  Velocity Calculation │ Real-time rotational velocity         │
├─────────────────────────────────────────────────────────────────┤
│  SPI Communication │ Configurable frame formats               │
├─────────────────────────────────────────────────────────────────┤
│  AS5047U Driver   │ Low-level sensor register control         │
└─────────────────────────────────────────────────────────────────┘
```

## 🚀 Quick Start

### Basic Angle Measurement

```cpp
#include "utils-and-drivers/driver-handlers/As5047uHandler.h"
#include "component-handlers/CommChannelsManager.h"

void as5047u_basic_example() {
    // Get SPI interface
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) {
        logger.Info("AS5047U", "SPI interface not available\n");
        return;
    }
    
    // Create AS5047U handler
    As5047uHandler handler(*spi);
    
    // Initialize handler
    if (handler.Initialize() != As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Failed to initialize AS5047U\n");
        return;
    }
    
    // Read angle measurement
    As5047uMeasurement measurement;
    if (handler.ReadMeasurement(measurement) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Angle: %u LSB (%.2f°)\n", measurement.angle_compensated, 
               measurement.angle_compensated * 360.0 / 16384.0);
        logger.Info("AS5047U", "Velocity: %.2f RPM\n", measurement.velocity_rpm);
    }
}
```

## 📖 API Reference

### Core Operations

#### Construction and Initialization
```cpp
class As5047uHandler {
public:
    // Constructor
    explicit As5047uHandler(BaseSpi& spi_interface, 
                           const As5047uConfig& config = GetDefaultConfig()) noexcept;

    // Initialization
    As5047uError Initialize() noexcept;
    As5047uError Deinitialize() noexcept;
    bool IsInitialized() const noexcept;
    bool IsSensorReady() const noexcept;
    std::shared_ptr<AS5047U> GetSensor() noexcept;
};
```

#### Sensor Measurements
```cpp
// Complete measurement data
As5047uError ReadMeasurement(As5047uMeasurement& measurement) noexcept;

// Angle measurements
As5047uError ReadAngle(uint16_t& angle) noexcept;
As5047uError ReadRawAngle(uint16_t& raw_angle) noexcept;

// Velocity measurements
As5047uError ReadVelocity(int16_t& velocity_lsb) noexcept;
As5047uError ReadVelocityDegPerSec(double& velocity_deg_per_sec) noexcept;
As5047uError ReadVelocityRadPerSec(double& velocity_rad_per_sec) noexcept;
As5047uError ReadVelocityRPM(double& velocity_rpm) noexcept;
```

#### Sensor Diagnostics
```cpp
// Diagnostic information
As5047uError ReadDiagnostics(As5047uDiagnostics& diagnostics) noexcept;
As5047uError ReadAGC(uint8_t& agc_value) noexcept;
As5047uError ReadMagnitude(uint16_t& magnitude) noexcept;
As5047uError ReadErrorFlags(uint16_t& error_flags) noexcept;
As5047uError IsMagneticFieldOK(bool& field_ok) noexcept;
```

#### Sensor Configuration
```cpp
// Basic configuration
As5047uError SetZeroPosition(uint16_t zero_position) noexcept;
As5047uError GetZeroPosition(uint16_t& zero_position) noexcept;
As5047uError SetRotationDirection(bool clockwise) noexcept;

// Advanced features
As5047uError SetDAEC(bool enable) noexcept;
As5047uError SetAdaptiveFilter(bool enable) noexcept;
As5047uError ConfigureInterface(bool enable_abi, bool enable_uvw, bool enable_pwm) noexcept;
As5047uError SetABIResolution(uint8_t resolution_bits) noexcept;
As5047uError SetUVWPolePairs(uint8_t pole_pairs) noexcept;
As5047uError SetHighTemperatureMode(bool enable) noexcept;
```

#### Advanced Features
```cpp
// OTP programming
As5047uError ProgramOTP() noexcept;
As5047uError PerformCalibration() noexcept;
As5047uError ResetToDefaults() noexcept;

// Configuration management
As5047uError UpdateConfiguration(const As5047uConfig& config) noexcept;
As5047uError GetConfiguration(As5047uConfig& config) noexcept;
```

#### Utility Methods
```cpp
// Information
const char* GetDescription() const noexcept;
As5047uError GetLastError() const noexcept;
void DumpDiagnostics() const noexcept;
static As5047uConfig GetDefaultConfig() noexcept;

// Angle conversion (14-bit LSB to degrees): angle_lsb * 360.0 / 16384.0
```

## 🎯 Hardware Support

### AS5047U Features

- **14-bit Resolution**: 0-16383 LSB per revolution (0.0219° resolution)
- **Absolute Position**: Non-volatile zero position programming
- **Velocity Measurement**: Real-time rotational velocity calculation
- **Dynamic Angle Error Compensation**: DAEC for improved accuracy
- **Magnetic Field Monitoring**: AGC and magnitude monitoring
- **Interface Outputs**: ABI incremental, UVW commutation, PWM output
- **SPI Communication**: Configurable frame formats (16/24/32-bit)
- **High Temperature**: Optional 150°C operation mode
- **OTP Programming**: One-time programmable settings

### SPI Frame Formats

The AS5047U supports multiple SPI frame formats:
- **16-bit**: Standard format for basic operations
- **24-bit**: Extended format with additional data
- **32-bit**: Full format with maximum information

## 📊 Examples

### Basic Angle Reading

```cpp
void basic_angle_example() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    As5047uHandler handler(*spi);
    if (handler.Initialize() != As5047uError::SUCCESS) return;
    
    // Read angle measurements
    uint16_t angle;
    if (handler.ReadAngle(angle) == As5047uError::SUCCESS) {
        double angle_degrees = angle * 360.0 / 16384.0;
        double angle_radians = angle * 2.0 * M_PI / 16384.0;
        
        logger.Info("AS5047U", "Angle: %u LSB (%.2f°, %.4f rad)\n", 
               angle, angle_degrees, angle_radians);
    }
    
    // Read velocity
    double velocity_rpm;
    if (handler.ReadVelocityRPM(velocity_rpm) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Velocity: %.2f RPM\n", velocity_rpm);
    }
}
```

### Complete Measurement Data

```cpp
void complete_measurement_example() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    As5047uHandler handler(*spi);
    if (handler.Initialize() != As5047uError::SUCCESS) return;
    
    // Read complete measurement data
    As5047uMeasurement measurement;
    if (handler.ReadMeasurement(measurement) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Complete measurement data:\n");
        logger.Info("AS5047U", "  Raw angle: %u LSB\n", measurement.angle_raw);
        logger.Info("AS5047U", "  Compensated angle: %u LSB\n", measurement.angle_compensated);
        logger.Info("AS5047U", "  Angle degrees: %.2f°\n", 
               measurement.angle_compensated * 360.0 / 16384.0);
        logger.Info("AS5047U", "  Velocity LSB: %d\n", measurement.velocity_raw);
        logger.Info("AS5047U", "  Velocity deg/s: %.2f\n", measurement.velocity_deg_per_sec);
        logger.Info("AS5047U", "  Velocity rad/s: %.4f\n", measurement.velocity_rad_per_sec);
        logger.Info("AS5047U", "  Velocity RPM: %.2f\n", measurement.velocity_rpm);
        logger.Info("AS5047U", "  AGC value: %u\n", measurement.agc_value);
        logger.Info("AS5047U", "  Magnitude: %u\n", measurement.magnitude);
        logger.Info("AS5047U", "  Error flags: 0x%04X\n", measurement.error_flags);
        logger.Info("AS5047U", "  Valid: %s\n", measurement.valid ? "Yes" : "No");
    }
}
```

### Diagnostics and Health Monitoring

```cpp
void diagnostics_example() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    As5047uHandler handler(*spi);
    if (handler.Initialize() != As5047uError::SUCCESS) return;
    
    // Read comprehensive diagnostics
    As5047uDiagnostics diagnostics;
    if (handler.ReadDiagnostics(diagnostics) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Sensor diagnostics:\n");
        logger.Info("AS5047U", "  Magnetic field OK: %s\n", diagnostics.magnetic_field_ok ? "Yes" : "No");
        logger.Info("AS5047U", "  AGC warning: %s\n", diagnostics.agc_warning ? "Yes" : "No");
        logger.Info("AS5047U", "  CORDIC overflow: %s\n", diagnostics.cordic_overflow ? "Yes" : "No");
        logger.Info("AS5047U", "  Offset compensation OK: %s\n", diagnostics.offset_compensation_ok ? "Yes" : "No");
        logger.Info("AS5047U", "  Communication OK: %s\n", diagnostics.communication_ok ? "Yes" : "No");
        logger.Info("AS5047U", "  Last error flags: 0x%04X\n", diagnostics.last_error_flags);
        logger.Info("AS5047U", "  Communication errors: %u\n", diagnostics.communication_errors);
        logger.Info("AS5047U", "  Total measurements: %u\n", diagnostics.total_measurements);
    }
    
    // Check magnetic field status
    bool field_ok;
    if (handler.IsMagneticFieldOK(field_ok) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Magnetic field status: %s\n", field_ok ? "OK" : "WEAK");
    }
    
    // Read AGC value
    uint8_t agc_value;
    if (handler.ReadAGC(agc_value) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "AGC value: %u (0-255)\n", agc_value);
    }
}
```

### Configuration and Calibration

```cpp
void configuration_example() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    As5047uHandler handler(*spi);
    if (handler.Initialize() != As5047uError::SUCCESS) return;
    
    // Set zero position
    uint16_t zero_position = 8192;  // 180 degrees
    if (handler.SetZeroPosition(zero_position) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Zero position set to %u LSB\n", zero_position);
    }
    
    // Enable DAEC
    if (handler.SetDAEC(true) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "DAEC enabled\n");
    }
    
    // Enable adaptive filtering
    if (handler.SetAdaptiveFilter(true) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Adaptive filtering enabled\n");
    }
    
    // Configure interface outputs
    if (handler.ConfigureInterface(true, false, false) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "ABI output enabled\n");
    }
    
    // Set ABI resolution
    if (handler.SetABIResolution(12) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "ABI resolution set to 12 bits\n");
    }
    
    // Set rotation direction
    if (handler.SetRotationDirection(true) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Clockwise rotation enabled\n");
    }
}
```

### Advanced Configuration

```cpp
void advanced_config_example() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    // Create custom configuration
    As5047uConfig config = As5047uHandler::GetDefaultConfig();
    config.frame_format = FrameFormat::FRAME_24BIT;
    config.crc_retries = 3;
    config.enable_daec = true;
    config.enable_adaptive_filter = true;
    config.zero_position = 0;
    config.enable_abi_output = true;
    config.enable_uvw_output = false;
    config.enable_pwm_output = false;
    config.abi_resolution_bits = 12;
    config.uvw_pole_pairs = 4;
    config.high_temperature_mode = false;
    
    As5047uHandler handler(*spi, config);
    if (handler.Initialize() != As5047uError::SUCCESS) return;
    
    // Verify configuration
    As5047uConfig current_config;
    if (handler.GetConfiguration(current_config) == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Current configuration:\n");
        logger.Info("AS5047U", "  Frame format: %d\n", static_cast<int>(current_config.frame_format));
        logger.Info("AS5047U", "  DAEC enabled: %s\n", current_config.enable_daec ? "Yes" : "No");
        logger.Info("AS5047U", "  Adaptive filter: %s\n", current_config.enable_adaptive_filter ? "Yes" : "No");
        logger.Info("AS5047U", "  Zero position: %u\n", current_config.zero_position);
        logger.Info("AS5047U", "  ABI output: %s\n", current_config.enable_abi_output ? "Yes" : "No");
        logger.Info("AS5047U", "  ABI resolution: %u bits\n", current_config.abi_resolution_bits);
    }
}
```

### Error Handling

```cpp
void error_handling_example() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    As5047uHandler handler(*spi);
    
    // Check initialization
    As5047uError result = handler.Initialize();
    if (result != As5047uError::SUCCESS) {
        logger.Info("AS5047U", "ERROR: Failed to initialize AS5047U: %s\n", 
               As5047uErrorToString(result));
        return;
    }
    
    // Safe angle reading
    uint16_t angle;
    result = handler.ReadAngle(angle);
    if (result != As5047uError::SUCCESS) {
        logger.Info("AS5047U", "ERROR: Failed to read angle: %s\n", As5047uErrorToString(result));
        return;
    }
    
    // Check sensor readiness
    if (!handler.IsSensorReady()) {
        logger.Info("AS5047U", "ERROR: Sensor not ready\n");
        return;
    }
    
    // Monitor error flags
    uint16_t error_flags;
    if (handler.ReadErrorFlags(error_flags) == As5047uError::SUCCESS) {
        if (error_flags != 0) {
            logger.Info("AS5047U", "WARNING: Error flags detected: 0x%04X\n", error_flags);
        }
    }
    
    // Check magnetic field
    bool field_ok;
    if (handler.IsMagneticFieldOK(field_ok) == As5047uError::SUCCESS) {
        if (!field_ok) {
            logger.Info("AS5047U", "WARNING: Weak magnetic field detected\n");
        }
    }
    
    logger.Info("AS5047U", "All operations successful\n");
}
```

## 🔍 Advanced Usage

### Continuous Monitoring

```cpp
void continuous_monitoring() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    As5047uHandler handler(*spi);
    if (handler.Initialize() != As5047uError::SUCCESS) return;
    
    // Continuous monitoring loop
    for (int i = 0; i < 1000; i++) {
        As5047uMeasurement measurement;
        if (handler.ReadMeasurement(measurement) == As5047uError::SUCCESS) {
            double angle_deg = measurement.angle_compensated * 360.0 / 16384.0;
            
            logger.Info("AS5047U", "Sample %d: Angle=%.2f°, Velocity=%.2f RPM, AGC=%u\n", 
                   i, angle_deg, measurement.velocity_rpm, measurement.agc_value);
            
            // Check for issues
            if (measurement.error_flags != 0) {
                logger.Info("AS5047U", "WARNING: Error flags: 0x%04X\n", measurement.error_flags);
            }
            
            if (measurement.agc_value < 50 || measurement.agc_value > 200) {
                logger.Info("AS5047U", "WARNING: AGC out of range: %u\n", measurement.agc_value);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz monitoring
    }
}
```

### Calibration and OTP Programming

```cpp
void calibration_example() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    auto* spi = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    if (!spi) return;
    
    As5047uHandler handler(*spi);
    if (handler.Initialize() != As5047uError::SUCCESS) return;
    
    // Perform calibration
    logger.Info("AS5047U", "Starting sensor calibration...\n");
    if (handler.PerformCalibration() == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Calibration completed successfully\n");
        
        // Program settings to OTP (WARNING: One-time operation)
        logger.Info("AS5047U", "Programming settings to OTP...\n");
        if (handler.ProgramOTP() == As5047uError::SUCCESS) {
            logger.Info("AS5047U", "Settings programmed to OTP successfully\n");
        } else {
            logger.Info("AS5047U", "Failed to program OTP\n");
        }
    } else {
        logger.Info("AS5047U", "Calibration failed\n");
    }
    
    // Reset to defaults if needed
    logger.Info("AS5047U", "Resetting to defaults...\n");
    if (handler.ResetToDefaults() == As5047uError::SUCCESS) {
        logger.Info("AS5047U", "Reset to defaults completed\n");
    }
}
```

### Multi-Sensor Integration

```cpp
void multi_sensor_integration() {
    auto& comm = CommChannelsManager::GetInstance();
    comm.EnsureInitialized();
    
    // Create multiple AS5047U handlers
    std::vector<std::unique_ptr<As5047uHandler>> handlers;
    
    // Create handlers for different SPI devices
    auto* spi1 = comm.GetSpiDevice(SpiDeviceId::AS5047U_POSITION_ENCODER);
    auto* spi2 = comm.GetSpiDevice(SpiDeviceId::EXTERNAL_DEVICE_1);
    
    if (spi1) {
        auto handler1 = std::make_unique<As5047uHandler>(*spi1);
        if (handler1->Initialize() == As5047uError::SUCCESS) {
            handlers.push_back(std::move(handler1));
        }
    }
    
    if (spi2) {
        auto handler2 = std::make_unique<As5047uHandler>(*spi2);
        if (handler2->Initialize() == As5047uError::SUCCESS) {
            handlers.push_back(std::move(handler2));
        }
    }
    
    // Process data from all sensors
    for (size_t i = 0; i < handlers.size(); i++) {
        As5047uMeasurement measurement;
        if (handlers[i]->ReadMeasurement(measurement) == As5047uError::SUCCESS) {
            double angle_deg = measurement.angle_compensated * 360.0 / 16384.0;
            logger.Info("AS5047U", "Sensor %zu: Angle=%.2f°, Velocity=%.2f RPM\n", 
                   i, angle_deg, measurement.velocity_rpm);
        }
    }
}
```

## 📚 See Also

- **[MotorController Documentation](../component-handlers/MOTOR_CONTROLLER_README.md)** - Motor management system
- **[CommChannelsManager Documentation](../component-handlers/COMM_CHANNELS_MANAGER_README.md)** - Communication interfaces
- **[TMC9660 Handler Documentation](TMC9660_HANDLER_README.md)** - Motor controller handler
- **[BNO08x Handler Documentation](BNO08X_HANDLER_README.md)** - IMU sensor handler

---

*This documentation is part of the HardFOC HAL system. For complete system documentation, see [Documentation Index](../../DOCUMENTATION_INDEX.md).*