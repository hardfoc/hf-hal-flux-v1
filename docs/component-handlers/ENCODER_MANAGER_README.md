# EncoderManager - Position Encoder System

<div align="center">

![Component](https://img.shields.io/badge/component-EncoderManager-blue.svg)
![Hardware](https://img.shields.io/badge/hardware-AS5047U%20Encoders-orange.svg)
![Interface](https://img.shields.io/badge/interface-SPI%20%7C%20I2C-green.svg)

**Unified position encoder management system with multi-sensor support**

</div>

## 📋 Overview

The `EncoderManager` is a comprehensive position encoder management system that provides unified access to position sensors across the HardFOC HAL platform. It supports AS5047U magnetic encoders, incremental encoders, and provides advanced features like position tracking, velocity calculation, and calibration.

### ✨ Key Features

- **🎯 Multi-Encoder Support**: AS5047U, incremental, and custom encoders
- **📊 Unified API**: Single interface for all position measurements
- **⚡ Real-time Tracking**: High-speed position and velocity monitoring
- **🔧 Automatic Calibration**: Built-in calibration and compensation
- **📈 Position History**: Position tracking and trend analysis
- **🛡️ Error Detection**: Sensor health and accuracy monitoring
- **⚙️ Advanced Configuration**: Flexible encoder configuration
- **🔍 Diagnostics**: Comprehensive sensor diagnostics

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                   EncoderManager                               │
├─────────────────────────────────────────────────────────────────┤
│  AS5047U Encoders │ Magnetic position sensors via SPI         │
├─────────────────────────────────────────────────────────────────┤
│  Incremental Encoders │ Quadrature and digital encoders       │
├─────────────────────────────────────────────────────────────────┤
│  Position Tracking │ Real-time position and velocity monitoring│
├─────────────────────────────────────────────────────────────────┤
│  Calibration System │ Automatic compensation and scaling       │
└─────────────────────────────────────────────────────────────────┘
```

## 🚀 Quick Start

### Basic Encoder Reading

```cpp
#include "component-handlers/EncoderManager.h"
#include "utils-and-drivers/driver-handlers/Logger.h"

void encoder_basic_example() {
    auto& logger = Logger::GetInstance();
    
    // Get encoder manager instance
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Read position from onboard AS5047U encoder (device index 0)
    uint16_t angle;
    if (encoder_manager.ReadAngle(0, angle) == As5047uError::SUCCESS) {
        logger.Info("ENCODER", "Onboard encoder angle: %u LSB", angle);
    }
    
    // Read position in degrees
    double angle_degrees;
    if (encoder_manager.ReadAngleDegrees(0, angle_degrees) == As5047uError::SUCCESS) {
        logger.Info("ENCODER", "Onboard encoder position: %.2f degrees", angle_degrees);
    }
    
    // Read velocity
    double velocity_rpm;
    if (encoder_manager.ReadVelocityRPM(0, velocity_rpm) == As5047uError::SUCCESS) {
        logger.Info("ENCODER", "Onboard encoder velocity: %.2f RPM", velocity_rpm);
    }
    
    // Get encoder handler for direct access
    As5047uHandler* handler = encoder_manager.GetAs5047uHandler(0);
    if (handler) {
        As5047uMeasurement measurement;
        if (handler->ReadMeasurement(measurement) == As5047uError::SUCCESS) {
            logger.Info("ENCODER", "Complete measurement - Angle: %.2f°, Velocity: %.2f RPM", measurement.angle_compensated * 360.0 / 16384.0, 
            measurement.velocity_rpm);
        }
    }
}
```

## 📖 API Reference

### Core Operations

#### Construction and Initialization
```cpp
class EncoderManager {
public:
    // Singleton access
    static EncoderManager& GetInstance() noexcept;
    
    // Initialization
    bool EnsureInitialized() noexcept;
    bool Initialize() noexcept;
    void Deinitialize() noexcept;
    bool IsInitialized() const noexcept;
};
```

#### Encoder Management
```cpp
// Encoder registration and discovery
EncoderError RegisterEncoder(const std::string& encoder_id, 
                            const EncoderConfig& config) noexcept;

EncoderError UnregisterEncoder(const std::string& encoder_id) noexcept;

std::vector<std::string> GetAvailableEncoders() const noexcept;

bool IsEncoderAvailable(const std::string& encoder_id) const noexcept;
```

#### Position Measurements
```cpp
// Basic position reading
EncoderError ReadPosition(const std::string& encoder_id, float& position_degrees) noexcept;

EncoderError ReadPositionRadians(const std::string& encoder_id, float& position_radians) noexcept;

EncoderError ReadRawPosition(const std::string& encoder_id, uint32_t& raw_position) noexcept;

// Multiple encoder readings
EncoderError ReadAllPositions(std::vector<EncoderReading>& readings) noexcept;

// Position with metadata
EncoderError ReadPositionWithMetadata(const std::string& encoder_id, 
                                    EncoderReading& reading) noexcept;
```

#### Velocity Measurements
```cpp
// Velocity reading
EncoderError ReadVelocity(const std::string& encoder_id, float& velocity_rpm) noexcept;

EncoderError ReadVelocityDegPerSec(const std::string& encoder_id, float& velocity_deg_per_sec) noexcept;

EncoderError ReadVelocityRadPerSec(const std::string& encoder_id, float& velocity_rad_per_sec) noexcept;

EncoderError ReadRawVelocity(const std::string& encoder_id, int32_t& raw_velocity) noexcept;
```

#### Calibration and Configuration
```cpp
// Calibration operations
EncoderError CalibrateEncoder(const std::string& encoder_id) noexcept;

EncoderError SetZeroPosition(const std::string& encoder_id, float zero_position_degrees) noexcept;

EncoderError GetZeroPosition(const std::string& encoder_id, float& zero_position_degrees) noexcept;

EncoderError SetDirection(const std::string& encoder_id, bool clockwise) noexcept;

// Configuration management
EncoderError UpdateEncoderConfig(const std::string& encoder_id, 
                               const EncoderConfig& config) noexcept;

EncoderError GetEncoderConfig(const std::string& encoder_id, 
                            EncoderConfig& config) noexcept;
```

#### Monitoring and Tracking
```cpp
// Position tracking
EncoderError StartTracking(const std::string& encoder_id, float update_rate_hz = 100.0f) noexcept;

EncoderError StopTracking(const std::string& encoder_id) noexcept;

EncoderError GetTrackingStatus(const std::string& encoder_id, bool& is_tracking) noexcept;

// Position history
EncoderError GetPositionHistory(const std::string& encoder_id, 
                              std::vector<PositionSample>& history) noexcept;

EncoderError ClearPositionHistory(const std::string& encoder_id) noexcept;
```

#### System Status and Diagnostics
```cpp
// System status
EncoderSystemStatus GetSystemStatus() const noexcept;

bool IsSystemHealthy() const noexcept;

// Encoder diagnostics
EncoderError GetEncoderDiagnostics(const std::string& encoder_id, 
                                 EncoderDiagnostics& diagnostics) noexcept;

// Performance statistics
EncoderStatistics GetStatistics() const noexcept;

void ResetStatistics() noexcept;
```

## 🎯 Hardware Support

### Supported Encoder Types

#### AS5047U Magnetic Encoders
- **Interface**: SPI via CommChannelsManager
- **Resolution**: 14-bit absolute position (0-16383 counts)
- **Accuracy**: ±0.5° typical
- **Update Rate**: Up to 10kHz
- **Features**: DAEC, AGC monitoring, temperature compensation

#### Incremental Encoders
- **Interface**: GPIO pins via GpioManager
- **Types**: Quadrature, single-channel, digital
- **Resolution**: Configurable (typically 100-10000 counts per revolution)
- **Update Rate**: Up to 100kHz
- **Features**: Direction detection, index pulse support

#### Custom Encoders
- **Interface**: Configurable (SPI, I2C, GPIO)
- **Resolution**: User-defined
- **Features**: Custom protocols and data formats

### Encoder Configuration

```cpp
struct EncoderConfig {
    std::string encoder_id;
    EncoderType encoder_type;
    std::string hardware_source;  // "SPI_DEVICE_1", "GPIO_PINS", etc.
    
    // AS5047U specific
    uint8_t spi_frame_format;     // 16/24/32-bit frame format
    bool enable_daec;             // Dynamic Angle Error Compensation
    bool enable_adaptive_filter;  // Adaptive filtering
    
    // Incremental encoder specific
    std::string channel_a_pin;    // Channel A GPIO pin
    std::string channel_b_pin;    // Channel B GPIO pin
    std::string index_pin;        // Index pulse GPIO pin (optional)
    uint32_t counts_per_revolution; // Encoder resolution
    
    // General settings
    float zero_position_degrees;  // Zero position offset
    bool clockwise_direction;     // Rotation direction
    float calibration_offset;     // Calibration offset
    
    // Tracking settings
    float update_rate_hz;         // Position tracking rate
    bool enable_tracking;         // Enable position tracking
    uint32_t history_size;        // Position history size
};
```

## 📊 Examples

### Basic AS5047U Encoder

```cpp
void basic_as5047u_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Configure AS5047U encoder
    EncoderConfig config;
    config.encoder_id = "MOTOR_ENCODER";
    config.encoder_type = EncoderType::AS5047U;
    config.hardware_source = "SPI_AS5047U_1";
    config.spi_frame_format = 24;  // 24-bit frame format
    config.enable_daec = true;
    config.enable_adaptive_filter = true;
    config.zero_position_degrees = 0.0f;
    config.clockwise_direction = true;
    config.update_rate_hz = 1000.0f;
    config.enable_tracking = true;
    
    if (encoder_manager.RegisterEncoder("MOTOR_ENCODER", config) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "AS5047U encoder registered\n");
    }
    
    // Read position and velocity
    float position, velocity;
    if (encoder_manager.ReadPosition("MOTOR_ENCODER", position) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Position: %.2f degrees\n", position);
    }
    
    if (encoder_manager.ReadVelocity("MOTOR_ENCODER", velocity) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Velocity: %.2f RPM\n", velocity);
    }
}
```

### Incremental Encoder Setup

```cpp
void incremental_encoder_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Configure incremental encoder
    EncoderConfig config;
    config.encoder_id = "QUADRATURE_ENCODER";
    config.encoder_type = EncoderType::INCREMENTAL;
    config.channel_a_pin = "ESP32_GPIO_4";
    config.channel_b_pin = "ESP32_GPIO_5";
    config.index_pin = "ESP32_GPIO_6";  // Optional index pulse
    config.counts_per_revolution = 1000;  // 1000 counts per revolution
    config.zero_position_degrees = 0.0f;
    config.clockwise_direction = true;
    config.update_rate_hz = 1000.0f;
    config.enable_tracking = true;
    
    if (encoder_manager.RegisterEncoder("QUADRATURE_ENCODER", config) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Incremental encoder registered\n");
    }
    
    // Start position tracking
    if (encoder_manager.StartTracking("QUADRATURE_ENCODER", 1000.0f) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Position tracking started at 1kHz\n");
    }
}
```

### Multi-Encoder System

```cpp
void multi_encoder_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Register multiple encoders
    std::vector<std::string> encoder_ids = {"ENCODER_1", "ENCODER_2", "ENCODER_3"};
    
    for (size_t i = 0; i < encoder_ids.size(); i++) {
        EncoderConfig config;
        config.encoder_id = encoder_ids[i];
        config.encoder_type = EncoderType::AS5047U;
        config.hardware_source = "SPI_AS5047U_" + std::to_string(i + 1);
        config.enable_daec = true;
        config.update_rate_hz = 500.0f;
        config.enable_tracking = true;
        
        encoder_manager.RegisterEncoder(encoder_ids[i], config);
    }
    
    // Read from all encoders
    std::vector<EncoderReading> readings;
    if (encoder_manager.ReadAllPositions(readings) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Multi-encoder readings:\n");
        for (const auto& reading : readings) {
            logger.Info("ENCODER", "  %s: Position=%.2f°, Velocity=%.2f RPM\n", 
                   reading.encoder_id.c_str(), 
                   reading.position_degrees, 
                   reading.velocity_rpm);
        }
    }
}
```

### Position Tracking and History

```cpp
void tracking_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Start position tracking
    if (encoder_manager.StartTracking("MOTOR_ENCODER", 100.0f) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Position tracking started at 100Hz\n");
    }
    
    // Collect position data for 10 seconds
    for (int i = 0; i < 1000; i++) {
        float position, velocity;
        if (encoder_manager.ReadPosition("MOTOR_ENCODER", position) == EncoderError::SUCCESS &&
            encoder_manager.ReadVelocity("MOTOR_ENCODER", velocity) == EncoderError::SUCCESS) {
            logger.Info("ENCODER", "Sample %d: Position=%.2f°, Velocity=%.2f RPM\n", i, position, velocity);
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz sampling
    }
    
    // Get position history
    std::vector<PositionSample> history;
    if (encoder_manager.GetPositionHistory("MOTOR_ENCODER", history) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Position history (%zu samples):\n", history.size());
        for (const auto& sample : history) {
            logger.Info("ENCODER", "  Time: %.3fs, Position: %.2f°, Velocity: %.2f RPM\n", 
                   sample.timestamp_ms / 1000.0f, 
                   sample.position_degrees, 
                   sample.velocity_rpm);
        }
    }
    
    // Stop tracking
    encoder_manager.StopTracking("MOTOR_ENCODER");
}
```

### Calibration and Configuration

```cpp
void calibration_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Perform encoder calibration
    if (encoder_manager.CalibrateEncoder("MOTOR_ENCODER") == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Encoder calibration completed\n");
    }
    
    // Set zero position
    if (encoder_manager.SetZeroPosition("MOTOR_ENCODER", 180.0f) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Zero position set to 180 degrees\n");
    }
    
    // Set rotation direction
    if (encoder_manager.SetDirection("MOTOR_ENCODER", true) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Clockwise rotation enabled\n");
    }
    
    // Verify configuration
    float zero_position;
    if (encoder_manager.GetZeroPosition("MOTOR_ENCODER", zero_position) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Current zero position: %.2f degrees\n", zero_position);
    }
    
    // Update encoder configuration
    EncoderConfig config;
    if (encoder_manager.GetEncoderConfig("MOTOR_ENCODER", config) == EncoderError::SUCCESS) {
        config.update_rate_hz = 2000.0f;  // Increase update rate
        config.enable_tracking = true;
        
        if (encoder_manager.UpdateEncoderConfig("MOTOR_ENCODER", config) == EncoderError::SUCCESS) {
            logger.Info("ENCODER", "Encoder configuration updated\n");
        }
    }
}
```

### System Diagnostics

```cpp
void diagnostics_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Get system status
    auto status = encoder_manager.GetSystemStatus();
    logger.Info("ENCODER", "Encoder system status:\n");
    logger.Info("ENCODER", "  Overall healthy: %s\n", status.overall_healthy ? "Yes" : "No");
    logger.Info("ENCODER", "  Active encoders: %u\n", status.active_encoders);
    logger.Info("ENCODER", "  Total encoders: %u\n", status.total_encoders);
    logger.Info("ENCODER", "  Tracking active: %s\n", status.tracking_active ? "Yes" : "No");
    
    // Get encoder diagnostics
    for (const auto& encoder_id : encoder_manager.GetAvailableEncoders()) {
        EncoderDiagnostics diagnostics;
        if (encoder_manager.GetEncoderDiagnostics(encoder_id, diagnostics) == EncoderError::SUCCESS) {
            logger.Info("ENCODER", "Encoder %s diagnostics:\n", encoder_id.c_str());
            logger.Info("ENCODER", "  Healthy: %s\n", diagnostics.healthy ? "Yes" : "No");
            logger.Info("ENCODER", "  Last position: %.2f degrees\n", diagnostics.last_position);
            logger.Info("ENCODER", "  Last velocity: %.2f RPM\n", diagnostics.last_velocity);
            logger.Info("ENCODER", "  Min position: %.2f degrees\n", diagnostics.min_position);
            logger.Info("ENCODER", "  Max position: %.2f degrees\n", diagnostics.max_position);
            logger.Info("ENCODER", "  Average velocity: %.2f RPM\n", diagnostics.average_velocity);
            logger.Info("ENCODER", "  Reading count: %u\n", diagnostics.reading_count);
            logger.Info("ENCODER", "  Error count: %u\n", diagnostics.error_count);
            logger.Info("ENCODER", "  Communication errors: %u\n", diagnostics.communication_errors);
        }
    }
    
    // Get performance statistics
    auto stats = encoder_manager.GetStatistics();
    logger.Info("ENCODER", "Performance statistics:\n");
    logger.Info("ENCODER", "  Total readings: %u\n", stats.total_readings);
    logger.Info("ENCODER", "  Successful readings: %u\n", stats.successful_readings);
    logger.Info("ENCODER", "  Failed readings: %u\n", stats.failed_readings);
    logger.Info("ENCODER", "  Average response time: %.2f ms\n", stats.average_response_time_ms);
    logger.Info("ENCODER", "  Peak response time: %.2f ms\n", stats.peak_response_time_ms);
}
```

## 🔍 Advanced Usage

### Custom Encoder Integration

```cpp
void custom_encoder_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Configure custom encoder
    EncoderConfig config;
    config.encoder_id = "CUSTOM_ENCODER";
    config.encoder_type = EncoderType::CUSTOM;
    config.hardware_source = "I2C_DEVICE_1";
    config.update_rate_hz = 100.0f;
    config.enable_tracking = true;
    
    if (encoder_manager.RegisterEncoder("CUSTOM_ENCODER", config) == EncoderError::SUCCESS) {
        logger.Info("ENCODER", "Custom encoder registered\n");
    }
}
```

### Position Analysis

```cpp
void position_analysis_example() {
    auto& encoder_manager = EncoderManager::GetInstance();
    encoder_manager.EnsureInitialized();
    
    // Collect position data over time
    std::vector<float> position_history;
    std::vector<float> velocity_history;
    const int samples = 1000;
    
    for (int i = 0; i < samples; i++) {
        float position, velocity;
        if (encoder_manager.ReadPosition("MOTOR_ENCODER", position) == EncoderError::SUCCESS &&
            encoder_manager.ReadVelocity("MOTOR_ENCODER", velocity) == EncoderError::SUCCESS) {
            position_history.push_back(position);
            velocity_history.push_back(velocity);
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz sampling
    }
    
    // Analyze position data
    if (position_history.size() >= 2) {
        float min_pos = *std::min_element(position_history.begin(), position_history.end());
        float max_pos = *std::max_element(position_history.begin(), position_history.end());
        float avg_pos = std::accumulate(position_history.begin(), position_history.end(), 0.0f) / position_history.size();
        
        float min_vel = *std::min_element(velocity_history.begin(), velocity_history.end());
        float max_vel = *std::max_element(velocity_history.begin(), velocity_history.end());
        float avg_vel = std::accumulate(velocity_history.begin(), velocity_history.end(), 0.0f) / velocity_history.size();
        
        logger.Info("ENCODER", "Position analysis:\n");
        logger.Info("ENCODER", "  Position range: %.2f° to %.2f° (%.2f° total)\n", min_pos, max_pos, max_pos - min_pos);
        logger.Info("ENCODER", "  Average position: %.2f°\n", avg_pos);
        logger.Info("ENCODER", "  Velocity range: %.2f to %.2f RPM\n", min_vel, max_vel);
        logger.Info("ENCODER", "  Average velocity: %.2f RPM\n", avg_vel);
        
        // Check for position stability
        if ((max_pos - min_pos) < 5.0f) {
            logger.Info("ENCODER", "Position is stable\n");
        } else {
            logger.Info("ENCODER", "Position is varying significantly\n");
        }
    }
}
```

## 📚 See Also

- **[CommChannelsManager Documentation](COMM_CHANNELS_MANAGER_README.md)** - SPI/I2C communication for encoders
- **[GpioManager Documentation](GPIO_MANAGER_README.md)** - GPIO interface for incremental encoders
- **[AS5047U Handler Documentation](../driver-handlers/AS5047U_HANDLER_README.md)** - AS5047U encoder driver
- **[MotorController Documentation](MOTOR_CONTROLLER_README.md)** - Motor control with encoder feedback

---

*This documentation is part of the HardFOC HAL system. For complete system documentation, see [Documentation Index](../../DOCUMENTATION_INDEX.md).*
