# HardFOC Flux Architecture Guidelines

<div align="center">

![Architecture Guidelines](https://img.shields.io/badge/Architecture%20Guidelines-HardFOC%20Flux%20HAL-blue?style=for-the-badge&logo=architecture)
![Version](https://img.shields.io/badge/version-2.0-green.svg)
![License](https://img.shields.io/badge/license-MIT-yellow.svg)

**Comprehensive architecture guidelines and design principles for the HardFOC Flux Hardware Abstraction Layer**

[📋 Overview](#overview) • [🏗️ System Architecture](#system-architecture) • [🔧 Design Patterns](#design-patterns) • [📊 Component Design](#component-design) • [🛡️ Safety & Reliability](#safety--reliability) • [⚡ Performance](#performance)

</div>

## 📋 Overview

This document defines the architectural principles, design patterns, and guidelines that govern the development of the HardFOC Flux HAL. These guidelines ensure a consistent, maintainable, and scalable architecture that supports the complex requirements of valve control applications.

### 🎯 Key Architectural Principles

- **Hardware Abstraction**: Complete separation of hardware-specific details from application logic
- **Modularity**: Well-defined interfaces and loose coupling between components
- **Thread Safety**: Safe concurrent access from multiple tasks
- **Error Resilience**: Comprehensive error handling and recovery mechanisms
- **Performance**: Optimized for real-time embedded systems
- **Extensibility**: Easy to add new hardware and features
- **Portability**: Platform-agnostic design where possible

---

## 🏗️ System Architecture

### Layered Architecture

The HardFOC Flux HAL follows a layered architecture pattern with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                       │
├─────────────────────────────────────────────────────────────────┤
│  API Layer           │ Public interfaces & integration        │
├─────────────────────────────────────────────────────────────────┤
│  Component Handlers  │ Managers: Comm, GPIO, Valve, ADC,      │
│                      │          LED, Temperature              │
├─────────────────────────────────────────────────────────────────┤
│  Driver Handlers     │ MAX22200, TLE92466ED, PCAL95555,       │
│                      │ ADS7952, WS2812, NTC                   │
├─────────────────────────────────────────────────────────────────┤
│  Hardware Drivers    │ ESP32-S3 interfaces & external drivers │
└─────────────────────────────────────────────────────────────────┘
```

### Layer Responsibilities

#### Application Layer
- **Purpose**: High-level business logic and application functionality
- **Hardware Knowledge**: **ZERO** - Uses only functional identifiers
- **Examples**: Valve control operations, system monitoring, user interface

#### API Layer
- **Purpose**: Public interfaces and system integration
- **Responsibilities**: 
  - System initialization and maintenance
  - Health monitoring and diagnostics
  - Unified access to all components
- **Files**: `API/Flux.h`

#### Component Handlers (Managers)
- **Purpose**: High-level singleton managers providing unified interfaces
- **Responsibilities**:
  - Resource management and coordination
  - Thread-safe operations
  - Error handling and recovery
  - Performance optimization
- **Examples**: `CommChannelsManager`, `GpioManager`, `ValveManager`, `AdcManager`, `LedManager`, `TemperatureManager`

#### Driver Handlers
- **Purpose**: Hardware-specific drivers providing device interfaces
- **Responsibilities**:
  - Device-specific communication
  - Hardware abstraction
  - Error handling and diagnostics
- **Examples**: `Max22200Handler`, `Tle92466edHandler`, `Pcal95555Handler`, `Ads7952Handler`

#### Hardware Drivers
- **Purpose**: Platform-specific implementations and external drivers
- **Responsibilities**:
  - Direct hardware interaction
  - Platform-specific optimizations
  - External driver integration

### Hardware Abstraction Strategy

#### Functional Pin Mapping
The system uses functional pin mapping to achieve complete hardware abstraction:

```cpp
// Application code uses functional identifiers
gpio.SetPin("VALVE_ENABLE", true);
adc.ReadVoltage("NTC_THERMISTOR");

// Hardware mapping is handled automatically
// "VALVE_ENABLE" → ESP32_GPIO_X
// "NTC_THERMISTOR" → ESP32_ADC_CHANNEL_Y
```

#### Platform Configuration
Hardware-specific details are isolated in configuration files:

```cpp
// Functional pin definitions (hardware-agnostic)
X(VALVE_ENABLE, "Valve Enable Pin", 2, 0, 2, 0, false, true, true, true, 20)
X(NTC_THERMISTOR, "NTC Thermistor ADC", 2, 2, 1, 0, false, false, false, true, 100)

// Platform mapping (hardware-specific)
// Format: FUNCTIONAL_PIN → CHIP_TYPE, PHYSICAL_PIN, UNIT_NUMBER
```

---

## 🔧 Design Patterns

### Singleton Pattern

#### Manager Classes
All manager classes use the singleton pattern for global access:

```cpp
class GpioManager {
public:
    static GpioManager& GetInstance() noexcept;
    
private:
    GpioManager() = default;
    ~GpioManager() = default;
    GpioManager(const GpioManager&) = delete;
    GpioManager& operator=(const GpioManager&) = delete;
};
```

#### Benefits
- **Global Access**: Single point of access to system resources
- **Resource Management**: Centralized resource allocation and cleanup
- **Thread Safety**: Proper synchronization for concurrent access
- **Lazy Initialization**: Resources initialized only when needed

### Bridge Pattern

#### Hardware Abstraction
The bridge pattern is used to separate abstraction from implementation:

```cpp
// Abstraction
class BaseGpio {
public:
    virtual ~BaseGpio() = default;
    virtual bool SetPin(bool state) noexcept = 0;
    virtual bool GetPin() const noexcept = 0;
};

// Implementation
class McuDigitalGpio : public BaseGpio {
    // ESP32-S3 specific implementation
};

class Pcal95555GpioWrapper : public BaseGpio {
    // I2C GPIO expander implementation
};
```

#### Benefits
- **Platform Independence**: Same interface across different hardware
- **Extensibility**: Easy to add new hardware implementations
- **Testability**: Mock implementations for testing
- **Maintainability**: Clear separation of concerns

### Factory Pattern

#### Handler Creation
Factory pattern for creating hardware handlers:

```cpp
class HandlerFactory {
public:
    static std::unique_ptr<BaseGpio> CreateGpioPin(
        std::string_view pin_name) noexcept;
    
    static std::unique_ptr<BaseAdc> CreateAdcChannel(
        std::string_view channel_name) noexcept;
};
```

#### Benefits
- **Encapsulation**: Creation logic hidden from clients
- **Flexibility**: Easy to change creation logic
- **Validation**: Centralized validation during creation
- **Resource Management**: Proper ownership transfer

### RAII (Resource Acquisition Is Initialization)

#### Resource Management
RAII ensures proper resource cleanup:

```cpp
class GpioGuard {
public:
    GpioGuard(std::string_view pin_name, bool initial_state);
    ~GpioGuard();  // Automatically restores pin state
    
private:
    std::string_view pin_name_;
    bool original_state_;
};
```

#### Benefits
- **Automatic Cleanup**: Resources released when objects go out of scope
- **Exception Safety**: Resources cleaned up even if exceptions occur
- **Simplified Code**: No manual cleanup required
- **Error Prevention**: Prevents resource leaks

---

## 📊 Component Design

### Manager Class Architecture

#### Standard Manager Structure
All manager classes follow a consistent structure:

```cpp
class ComponentManager {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================
    
    static ComponentManager& GetInstance() noexcept;
    
    //==========================================================================
    // INITIALIZATION AND CONFIGURATION
    //==========================================================================
    
    bool Initialize() noexcept;
    void Deinitialize() noexcept;
    bool IsInitialized() const noexcept;
    
    //==========================================================================
    // CORE OPERATIONS
    //==========================================================================
    
    // Core functionality methods
    
    //==========================================================================
    // DIAGNOSTICS AND MONITORING
    //==========================================================================
    
    // Diagnostic methods
    
private:
    //==========================================================================
    // PRIVATE IMPLEMENTATION
    //==========================================================================
    
    // Private methods and data members
};
```

#### Key Design Principles

##### Thread Safety
- **Mutex Protection**: All public methods protected by mutex
- **Atomic Operations**: Statistics and counters use atomic operations
- **Lock-Free Design**: Where possible, use lock-free data structures

```cpp
class ThreadSafeManager {
public:
    bool SetValue(int value) noexcept {
        std::lock_guard<RtosMutex> lock(mutex_);
        value_ = value;
        return true;
    }
    
private:
    mutable RtosMutex mutex_;
    int value_;
    std::atomic<uint32_t> operation_count_{0};
};
```

##### Error Handling
- **Comprehensive Error Codes**: Detailed error enumeration
- **Error Propagation**: Consistent error handling throughout
- **Error Recovery**: Built-in recovery mechanisms where possible

```cpp
enum class ComponentError : uint8_t {
    SUCCESS = 0,
    FAILURE = 1,
    NOT_INITIALIZED = 2,
    INVALID_PARAMETER = 3,
    HARDWARE_FAULT = 4,
    TIMEOUT = 5,
    // ... additional errors
};
```

##### Performance Optimization
- **Lazy Initialization**: Resources initialized only when needed
- **Caching**: Frequently accessed data cached
- **Batch Operations**: Multiple operations performed together
- **Memory Pooling**: Reuse of frequently allocated objects

### Handler Class Architecture

#### Standard Handler Structure
Hardware handlers follow a consistent pattern:

```cpp
class HardwareHandler {
public:
    //==========================================================================
    // CONSTRUCTION AND DESTRUCTION
    //==========================================================================
    
    explicit HardwareHandler(CommunicationInterface& comm) noexcept;
    ~HardwareHandler() = default;
    
    //==========================================================================
    // INITIALIZATION AND CONFIGURATION
    //==========================================================================
    
    bool Initialize() noexcept;
    bool IsInitialized() const noexcept;
    
    //==========================================================================
    // CORE OPERATIONS
    //==========================================================================
    
    // Device-specific operations
    
    //==========================================================================
    // DIAGNOSTICS AND MONITORING
    //==========================================================================
    
    bool IsHealthy() const noexcept;
    ComponentError GetLastError() const noexcept;
    
private:
    //==========================================================================
    // PRIVATE IMPLEMENTATION
    //==========================================================================
    
    CommunicationInterface& comm_;
    bool initialized_{false};
    ComponentError last_error_{ComponentError::SUCCESS};
    mutable RtosMutex mutex_;
};
```

#### Key Design Principles

##### Communication Abstraction
- **Interface-Based**: Use abstract communication interfaces
- **Protocol Independence**: Support multiple communication protocols
- **Error Handling**: Comprehensive communication error handling

```cpp
class Max22200Handler {
public:
    explicit Max22200Handler(BaseSpi& spi) noexcept;
    
private:
    BaseSpi& spi_;
    // Handler implementation
};
```

##### State Management
- **Initialization State**: Track initialization status
- **Error State**: Maintain last error information
- **Health State**: Monitor device health

##### Resource Management
- **RAII**: Proper resource cleanup
- **Smart Pointers**: Automatic memory management
- **Reference Counting**: Shared resource management

---

## 🛡️ Safety & Reliability

### Error Handling Strategy

#### Comprehensive Error Codes
Use X-macro pattern for consistent error enumeration:

```cpp
#define HF_GPIO_ERR_LIST(X) \
    X(GPIO_SUCCESS, 0, "Success") \
    X(GPIO_ERR_FAILURE, 1, "General failure") \
    X(GPIO_ERR_NOT_INITIALIZED, 2, "Not initialized") \
    X(GPIO_ERR_INVALID_PARAMETER, 3, "Invalid parameter") \
    X(GPIO_ERR_HARDWARE_FAULT, 4, "Hardware fault") \
    X(GPIO_ERR_TIMEOUT, 5, "Operation timeout") \
    // ... additional errors

enum class hf_gpio_err_t : uint8_t {
    #define X(name, value, desc) name = value,
    HF_GPIO_ERR_LIST(X)
    #undef X
};
```

#### Error Validation
- **Input Validation**: Validate all input parameters
- **State Validation**: Check component state before operations
- **Hardware Validation**: Verify hardware status

```cpp
hf_gpio_err_t ValidatePinName(std::string_view name) noexcept {
    if (name.empty()) {
        return hf_gpio_err_t::GPIO_ERR_INVALID_PARAMETER;
    }
    
    // Check maximum length
    static constexpr size_t MAX_PIN_NAME_LENGTH = 32;
    if (name.length() > MAX_PIN_NAME_LENGTH) {
        return hf_gpio_err_t::GPIO_ERR_OUT_OF_MEMORY;
    }
    
    // Check for reserved prefixes
    static constexpr std::array<std::string_view, 4> RESERVED_PREFIXES = {{
        "CORE_", "COMM_", "SYS_", "INTERNAL_"
    }};
    
    for (const auto& prefix : RESERVED_PREFIXES) {
        if (name.length() >= prefix.length() && 
            name.substr(0, prefix.length()) == prefix) {
            return hf_gpio_err_t::GPIO_ERR_PERMISSION_DENIED;
        }
    }
    
    return hf_gpio_err_t::GPIO_SUCCESS;
}
```

### Exception Safety

#### No-Exception Guarantee
- **No Exceptions**: All functions marked `noexcept`
- **Error Codes**: Use return codes for error handling
- **Resource Safety**: RAII ensures resource cleanup

```cpp
bool SetPin(std::string_view pin_name, bool state) noexcept {
    auto error = ValidatePinName(pin_name);
    if (error != hf_gpio_err_t::GPIO_SUCCESS) {
        UpdateLastError(error);
        return false;
    }
    
    // Implementation with error handling
    return true;
}
```

### Thread Safety

#### Mutex Protection
- **Public Methods**: All public methods protected by mutex
- **Private Methods**: Internal methods may be unprotected
- **Performance**: Minimize critical section size

```cpp
class ThreadSafeComponent {
public:
    bool SetValue(int value) noexcept {
        std::lock_guard<RtosMutex> lock(mutex_);
        value_ = value;
        UpdateStatistics(true);
        return true;
    }
    
private:
    void UpdateStatistics(bool success) noexcept {
        // No mutex needed - called from protected context
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        if (success) {
            successful_operations_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    mutable RtosMutex mutex_;
    int value_;
    std::atomic<uint32_t> total_operations_{0};
    std::atomic<uint32_t> successful_operations_{0};
};
```

#### Atomic Operations
- **Statistics**: Use atomic operations for counters
- **Flags**: Use atomic operations for status flags
- **Memory Ordering**: Use appropriate memory ordering

### Fault Tolerance

#### Hardware Fault Detection
- **Communication Errors**: Detect and handle communication failures
- **Hardware Faults**: Monitor hardware status
- **Recovery Mechanisms**: Automatic recovery where possible

#### Graceful Degradation
- **Partial Failures**: Continue operation with reduced functionality
- **Error Reporting**: Comprehensive error reporting
- **Fallback Mechanisms**: Alternative operation modes

---

## ⚡ Performance

### Optimization Strategies

#### Memory Management
- **Stack Allocation**: Prefer stack allocation for small objects
- **Memory Pooling**: Reuse frequently allocated objects
- **Smart Pointers**: Use smart pointers for dynamic allocation
- **Move Semantics**: Use move semantics to avoid copies

```cpp
// Prefer stack allocation
std::array<uint8_t, 64> buffer;

// Use smart pointers for dynamic allocation
std::unique_ptr<BaseGpio> gpio_pin_;

// Use move semantics
std::vector<uint8_t> data = std::move(another_vector);
```

#### Algorithm Optimization
- **Efficient Algorithms**: Choose appropriate algorithms
- **Cache-Friendly**: Optimize for cache performance
- **Branch Prediction**: Minimize unpredictable branches
- **Loop Optimization**: Optimize loop performance

#### Communication Optimization
- **Batch Operations**: Group multiple operations
- **DMA Usage**: Use DMA for large data transfers
- **Interrupt Optimization**: Minimize interrupt overhead
- **Protocol Efficiency**: Choose efficient communication protocols

### Performance Monitoring

#### Metrics Collection
- **Operation Counts**: Track operation statistics
- **Timing Information**: Measure operation timing
- **Error Rates**: Monitor error frequencies
- **Resource Usage**: Track memory and CPU usage

```cpp
class PerformanceMetrics {
public:
    void RecordOperation(uint32_t duration_us) noexcept {
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        total_duration_.fetch_add(duration_us, std::memory_order_relaxed);
        
        // Update min/max timing
        uint32_t current_min = min_duration_.load(std::memory_order_relaxed);
        while (duration_us < current_min && 
               !min_duration_.compare_exchange_weak(current_min, duration_us,
                                                   std::memory_order_relaxed)) {
            // Retry if value changed
        }
        
        uint32_t current_max = max_duration_.load(std::memory_order_relaxed);
        while (duration_us > current_max && 
               !max_duration_.compare_exchange_weak(current_max, duration_us,
                                                   std::memory_order_relaxed)) {
            // Retry if value changed
        }
    }
    
private:
    std::atomic<uint32_t> total_operations_{0};
    std::atomic<uint32_t> total_duration_{0};
    std::atomic<uint32_t> min_duration_{UINT32_MAX};
    std::atomic<uint32_t> max_duration_{0};
};
```

#### Performance Analysis
- **Profiling**: Use profiling tools to identify bottlenecks
- **Benchmarking**: Regular performance benchmarking
- **Regression Testing**: Detect performance regressions
- **Optimization Tracking**: Track optimization effectiveness

### Real-Time Considerations

#### Deterministic Behavior
- **Predictable Timing**: Ensure predictable operation timing
- **Bounded Latency**: Guarantee maximum latency bounds
- **Priority Management**: Proper task priority management
- **Resource Reservation**: Reserve resources for critical operations

#### Interrupt Handling
- **Minimal ISR**: Keep interrupt service routines minimal
- **Deferred Processing**: Defer complex processing to tasks
- **Interrupt Priority**: Proper interrupt priority management
- **Interrupt Safety**: Ensure interrupt-safe operations

---

## 🔍 Architecture Review Checklist

### Design Principles
- [ ] Hardware abstraction achieved
- [ ] Modular design with clear interfaces
- [ ] Thread safety implemented
- [ ] Error handling comprehensive
- [ ] Performance optimized
- [ ] Extensible design

### Component Design
- [ ] Consistent class structure
- [ ] Proper separation of concerns
- [ ] Appropriate design patterns used
- [ ] Resource management implemented
- [ ] Error handling strategy defined

### Safety & Reliability
- [ ] Comprehensive error codes
- [ ] Exception safety guaranteed
- [ ] Thread safety verified
- [ ] Fault tolerance implemented
- [ ] Recovery mechanisms defined

### Performance
- [ ] Memory usage optimized
- [ ] Algorithm efficiency verified
- [ ] Communication optimized
- [ ] Real-time requirements met
- [ ] Performance monitoring implemented

---

## 📋 Summary

These architecture guidelines ensure that the HardFOC Flux HAL:

1. **Achieves True Hardware Abstraction** - Complete separation of hardware details from application logic
2. **Provides Consistent Interfaces** - Uniform patterns across all components
3. **Ensures Thread Safety** - Safe concurrent access from multiple tasks
4. **Implements Robust Error Handling** - Comprehensive error detection and recovery
5. **Optimizes Performance** - Efficient operation for real-time systems
6. **Supports Extensibility** - Easy to add new hardware and features

Following these guidelines will result in a maintainable, scalable, and reliable architecture that meets the complex requirements of valve control applications.

---

<div align="center">

**For implementation details, please refer to the [Coding Standards](CODING_STANDARDS.md) or contact the HardFOC development team.**

</div> 
