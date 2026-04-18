# HardFOC Flux Coding Standards

<div align="center">

![Coding Standards](https://img.shields.io/badge/Coding%20Standards-HardFOC%20Flux%20HAL-blue?style=for-the-badge&logo=cpp)
![Version](https://img.shields.io/badge/version-2.0-green.svg)
![License](https://img.shields.io/badge/license-MIT-yellow.svg)

**Comprehensive coding standards and conventions for the HardFOC Flux Hardware Abstraction Layer**

[📋 Overview](#overview) • [🏗️ Architecture](#architecture) • [📝 Naming Conventions](#naming-conventions) • [🔧 Code Style](#code-style) • [🛡️ Error Handling](#error-handling) • [📚 Documentation](#documentation)

</div>

## 📋 Overview

This document defines the coding standards and conventions used throughout the HardFOC Flux HAL codebase. These standards ensure consistency, maintainability, and readability across all components while promoting best practices for embedded systems development.

### 🎯 Key Principles

- **Consistency**: Uniform patterns across all code files
- **Readability**: Clear, self-documenting code
- **Maintainability**: Easy to understand and modify
- **Performance**: Optimized for embedded systems
- **Safety**: Robust error handling and validation
- **Portability**: Platform-agnostic design where possible

---

## 🏗️ Architecture

### Design Patterns

#### Singleton Pattern
All manager classes use the singleton pattern with thread-safe access:

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

#### RAII (Resource Acquisition Is Initialization)
Use RAII for resource management:

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

#### Bridge Pattern
Use bridge pattern for hardware abstraction:

```cpp
class BaseGpio {
public:
    virtual ~BaseGpio() = default;
    virtual bool SetPin(bool state) noexcept = 0;
    virtual bool GetPin() const noexcept = 0;
};

class McuDigitalGpio : public BaseGpio {
    // ESP32-C6 specific implementation
};
```

### Class Organization

#### Header File Structure
```cpp
/**
 * @file ComponentName.h
 * @brief Brief description of the component
 * 
 * @details Detailed description including key features,
 *          architecture, and usage examples.
 * 
 * @author HardFOC Team
 * @date 2025
 * @version 2.0
 * 
 * Key Features:
 * - Feature 1 description
 * - Feature 2 description
 * - Feature 3 description
 * 
 * Architecture:
 * - Design pattern used
 * - Thread safety considerations
 * - Error handling approach
 * 
 * @note Important usage notes
 * @warning Critical warnings
 */

#pragma once

// System includes
#include <cstdint>
#include <string_view>
#include <memory>

// Project includes
#include "base/BaseInterface.h"
#include "RtosMutex.h"

// Forward declarations
class SomeClass;

//==============================================================================
// CONSTANTS AND TYPES
//==============================================================================

/**
 * @brief Error codes for ComponentName operations
 */
enum class ComponentError : uint8_t {
    SUCCESS = 0,
    FAILURE = 1,
    NOT_INITIALIZED = 2,
    // ... other error codes
};

//==============================================================================
// MAIN CLASS
//==============================================================================

/**
 * @class ComponentName
 * @brief Detailed class description
 * 
 * Thread Safety:
 * - Thread safety considerations
 * 
 * Error Handling:
 * - Error handling approach
 * 
 * Performance:
 * - Performance characteristics
 * 
 * Platform Integration:
 * - Platform-specific considerations
 */
class ComponentName {
public:
    //==========================================================================
    // SINGLETON AND LIFECYCLE
    //==========================================================================
    
    static ComponentName& GetInstance() noexcept;
    
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

---

## 📝 Naming Conventions

### File Naming

| Type | Convention | Example |
|------|------------|---------|
| Header Files | `PascalCase.h` | `GpioManager.h` |
| Source Files | `PascalCase.cpp` | `GpioManager.cpp` |
| Test Files | `PascalCaseTest.h/cpp` | `GpioManagerTest.h` |
| Documentation | `UPPER_CASE.md` | `CODING_STANDARDS.md` |

### Class Naming

| Type | Convention | Example |
|------|------------|---------|
| Manager Classes | `PascalCaseManager` | `GpioManager`, `AdcManager`, `ValveManager` |
| Handler Classes | `PascalCaseHandler` | `Max22200Handler`, `Tle92466edHandler` |
| Base Classes | `BasePascalCase` | `BaseGpio`, `BaseAdc` |
| MCU Implementations | `McuPascalCase` | `McuDigitalGpio`, `McuAdc` |
| Utility Classes | `PascalCase` | `Logger`, `RtosMutex` |

### Function Naming

| Type | Convention | Example |
|------|------------|---------|
| Public Methods | `PascalCase` | `SetPin()`, `GetPin()`, `Initialize()` |
| Private Methods | `PascalCase` | `ValidatePinName()`, `UpdateStatistics()` |
| Static Methods | `PascalCase` | `GetInstance()`, `CreateHandler()` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_PIN_NAME_LENGTH`, `DEFAULT_TIMEOUT_MS` |

### Variable Naming

| Type | Convention | Example |
|------|------------|---------|
| Member Variables | `snake_case_` | `total_operations_`, `last_error_` |
| Local Variables | `snake_case` | `pin_state`, `error_code` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_RETRY_COUNT`, `DEFAULT_FREQUENCY` |
| Enums | `PascalCase` | `GpioError`, `AdcChannel` |

### Error Code Naming

```cpp
enum class ComponentError : uint8_t {
    SUCCESS = 0,                    // Always first
    FAILURE = 1,                    // General failure
    NOT_INITIALIZED = 2,            // Component not initialized
    INVALID_PARAMETER = 3,          // Invalid input parameter
    HARDWARE_FAULT = 4,             // Hardware-related error
    TIMEOUT = 5,                    // Operation timeout
    OUT_OF_MEMORY = 6,              // Memory allocation failure
    PERMISSION_DENIED = 7,          // Access denied
    INVALID_ARG = 8,                // Invalid argument
    // ... additional specific errors
};
```

---

## 🔧 Code Style

### General Formatting

#### Indentation
- Use 4 spaces for indentation (no tabs)
- Align with opening brace
- Indent continuation lines by 4 spaces

```cpp
if (condition) {
    // 4-space indentation
    if (nested_condition) {
        // 8-space indentation
        do_something();
    }
}
```

#### Line Length
- Maximum 120 characters per line
- Break long lines at logical points
- Align continuation lines appropriately

```cpp
bool GpioManager::ConfigurePin(std::string_view pin_name,
                              bool is_input,
                              bool pull_up) noexcept {
    // Method implementation
}
```

#### Spacing
- No spaces around unary operators
- Spaces around binary operators
- Spaces after commas
- No spaces around scope resolution operator

```cpp
int result = a + b * c;  // Binary operators
int* ptr = &value;       // Unary operators
std::string name = "test";  // Assignment
GpioManager::GetInstance();  // Scope resolution
```

### Control Structures

#### If Statements
```cpp
if (condition) {
    // Single statement
}

if (condition) {
    // Multiple statements
    do_something();
    do_something_else();
} else if (other_condition) {
    // Alternative condition
    do_alternative();
} else {
    // Default case
    do_default();
}
```

#### Loops
```cpp
for (int i = 0; i < count; ++i) {
    // Loop body
}

while (condition) {
    // Loop body
}

do {
    // Loop body
} while (condition);
```

#### Switch Statements
```cpp
switch (value) {
    case 0:
        // Case 0
        break;
        
    case 1:
        // Case 1
        break;
        
    default:
        // Default case
        break;
}
```

### Function Definitions

#### Function Signature
```cpp
ReturnType ClassName::FunctionName(ParameterType parameter) noexcept {
    // Function implementation
}
```

#### Parameter Passing
- Use `const` references for large objects
- Use `std::string_view` for string parameters
- Use `noexcept` for functions that don't throw
- Use `const` member functions where appropriate

```cpp
bool SetPin(std::string_view pin_name, bool state) noexcept;
void ProcessData(const std::vector<uint8_t>& data) noexcept;
bool IsValid() const noexcept;
```

### Memory Management

#### Smart Pointers
- Prefer `std::unique_ptr` for exclusive ownership
- Use `std::shared_ptr` for shared ownership
- Avoid raw pointers for ownership

```cpp
std::unique_ptr<BaseGpio> gpio_pin_;
std::shared_ptr<BaseAdc> adc_channel_;
```

#### RAII
- Use RAII for all resource management
- Implement proper destructors
- Use move semantics where appropriate

```cpp
class ResourceGuard {
public:
    ResourceGuard(Resource* resource) : resource_(resource) {}
    ~ResourceGuard() { 
        if (resource_) resource_->Release(); 
    }
    
    // Delete copy constructor/assignment
    ResourceGuard(const ResourceGuard&) = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;
    
    // Allow move semantics
    ResourceGuard(ResourceGuard&&) = default;
    ResourceGuard& operator=(ResourceGuard&&) = default;
    
private:
    Resource* resource_;
};
```

---

## 🛡️ Error Handling

### Error Code Strategy

#### Comprehensive Error Codes
Use X-macro pattern for consistent error enumeration:

```cpp
#define HF_GPIO_ERR_LIST(X) \
    X(GPIO_SUCCESS, 0, "Success") \
    X(GPIO_ERR_FAILURE, 1, "General failure") \
    X(GPIO_ERR_NOT_INITIALIZED, 2, "Not initialized") \
    X(GPIO_ERR_INVALID_PARAMETER, 3, "Invalid parameter") \
    // ... additional errors

enum class hf_gpio_err_t : uint8_t {
    #define X(name, value, desc) name = value,
    HF_GPIO_ERR_LIST(X)
    #undef X
};
```

#### Error Validation Functions
```cpp
hf_gpio_err_t ValidatePinName(std::string_view name) noexcept {
    if (name.empty()) {
        return hf_gpio_err_t::GPIO_ERR_INVALID_PARAMETER;
    }
    
    // Additional validation logic
    return hf_gpio_err_t::GPIO_SUCCESS;
}
```

### Exception Safety

#### No-Exception Guarantee
- Use `noexcept` for all functions that don't throw
- Handle errors through return codes
- Avoid exceptions in embedded systems

```cpp
bool SetPin(std::string_view pin_name, bool state) noexcept {
    auto error = ValidatePinName(pin_name);
    if (error != hf_gpio_err_t::GPIO_SUCCESS) {
        UpdateLastError(error);
        return false;
    }
    
    // Implementation
    return true;
}
```

#### Error Propagation
```cpp
Result<bool> SetPin(std::string_view pin_name, bool state) noexcept {
    auto validation_result = ValidatePinName(pin_name);
    if (!validation_result.IsSuccess()) {
        return Result<bool>::Failure(validation_result.GetError());
    }
    
    // Implementation
    return Result<bool>::Success(true);
}
```

### Thread Safety

#### Mutex Protection
```cpp
class ThreadSafeComponent {
public:
    bool SetValue(int value) noexcept {
        std::lock_guard<RtosMutex> lock(mutex_);
        value_ = value;
        return true;
    }
    
private:
    mutable RtosMutex mutex_;
    int value_;
};
```

#### Atomic Operations
```cpp
class Statistics {
public:
    void UpdateCount(bool success) noexcept {
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        
        if (success) {
            successful_operations_.fetch_add(1, std::memory_order_relaxed);
        } else {
            failed_operations_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
private:
    std::atomic<uint32_t> total_operations_{0};
    std::atomic<uint32_t> successful_operations_{0};
    std::atomic<uint32_t> failed_operations_{0};
};
```

---

## 📚 Documentation

### File Headers
Every source file must have a comprehensive header:

```cpp
/**
 * @file ComponentName.h
 * @brief Brief description of the component
 * 
 * @details Detailed description including:
 * - Key features and capabilities
 * - Architecture and design patterns
 * - Thread safety considerations
 * - Error handling approach
 * - Performance characteristics
 * - Platform integration details
 * 
 * @author HardFOC Team
 * @date 2025
 * @version 2.0
 * @copyright HardFOC
 * 
 * Key Features:
 * - Feature 1: Description
 * - Feature 2: Description
 * - Feature 3: Description
 * 
 * Architecture:
 * - Design pattern used
 * - Thread safety approach
 * - Error handling strategy
 * 
 * @note Important usage notes
 * @warning Critical warnings
 * @see Related documentation
 */
```

### Class Documentation
```cpp
/**
 * @class ComponentName
 * @brief Brief class description
 * 
 * @details Detailed class description including:
 * - Purpose and responsibilities
 * - Design patterns used
 * - Thread safety considerations
 * - Error handling approach
 * - Performance characteristics
 * 
 * Thread Safety:
 * - Thread safety considerations
 * - Concurrent access patterns
 * - Locking mechanisms
 * 
 * Error Handling:
 * - Error code strategy
 * - Validation approach
 * - Recovery mechanisms
 * 
 * Performance:
 * - Performance characteristics
 * - Optimization strategies
 * - Resource usage
 * 
 * Platform Integration:
 * - Platform-specific considerations
 * - Hardware abstraction approach
 * - Portability considerations
 * 
 * Usage Example:
 * @code
 * auto& component = ComponentName::GetInstance();
 * component.Initialize();
 * component.SetValue(42);
 * @endcode
 */
```

### Method Documentation
```cpp
/**
 * @brief Brief method description
 * 
 * @details Detailed method description including:
 * - Purpose and functionality
 * - Parameter descriptions
 * - Return value details
 * - Error conditions
 * - Performance considerations
 * 
 * @param param1 Description of first parameter
 * @param param2 Description of second parameter
 * @return Description of return value
 * @retval true Success case description
 * @retval false Failure case description
 * 
 * @note Important usage notes
 * @warning Critical warnings
 * @see Related methods or documentation
 * 
 * @throws No exceptions thrown (noexcept)
 * 
 * Thread Safety:
 * - Thread safety considerations
 * - Locking behavior
 * 
 * Performance:
 * - Time complexity
 * - Space complexity
 * - Optimization notes
 * 
 * Example:
 * @code
 * auto result = component.SetValue(42);
 * if (result) {
 *     // Success case
 * } else {
 *     // Error handling
 * }
 * @endcode
 */
```

### Inline Documentation
```cpp
// Validate pin name format and length
if (name.empty()) {
    return hf_gpio_err_t::GPIO_ERR_INVALID_PARAMETER;
}

// Check maximum length to conserve memory
static constexpr size_t MAX_PIN_NAME_LENGTH = 32;
if (name.length() > MAX_PIN_NAME_LENGTH) {
    return hf_gpio_err_t::GPIO_ERR_OUT_OF_MEMORY;
}

// Check for reserved prefixes using compile-time array
static constexpr std::array<std::string_view, 4> RESERVED_PREFIXES = {{
    "CORE_", "COMM_", "SYS_", "INTERNAL_"
}};
```

---

## 🔍 Code Review Checklist

### General
- [ ] Code follows naming conventions
- [ ] Proper indentation and formatting
- [ ] No compiler warnings
- [ ] No memory leaks
- [ ] Thread safety considered
- [ ] Error handling implemented

### Documentation
- [ ] File header present and complete
- [ ] Class documentation comprehensive
- [ ] Method documentation detailed
- [ ] Inline comments for complex logic
- [ ] Usage examples provided

### Performance
- [ ] No unnecessary allocations
- [ ] Efficient algorithms used
- [ ] Proper const usage
- [ ] Move semantics where appropriate
- [ ] No performance bottlenecks

### Safety
- [ ] Input validation implemented
- [ ] Error codes comprehensive
- [ ] Exception safety considered
- [ ] Resource cleanup guaranteed
- [ ] Thread safety verified

---

## 📋 Summary

These coding standards ensure that all HardFOC Flux HAL code is:

1. **Consistent** - Uniform patterns across all components
2. **Readable** - Clear, self-documenting code
3. **Maintainable** - Easy to understand and modify
4. **Performant** - Optimized for embedded systems
5. **Safe** - Robust error handling and validation
6. **Portable** - Platform-agnostic design where possible

Following these standards will result in high-quality, maintainable code that contributes to the overall success of the HardFOC Flux project.

---

<div align="center">

**For questions or clarifications, please refer to the [Architecture Guidelines](ARCHITECTURE_GUIDELINES.md) or contact the HardFOC development team.**

</div> 
