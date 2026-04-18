# Embedded Systems Coding Standards

**C/C++ Coding Standards for Embedded Systems Development**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Document Purpose and Scope](#document-purpose-and-scope)
3. [Core Principles](#core-principles)
4. [Naming Conventions](#naming-conventions)
   - [Functions](#functions)
   - [Member Variables](#member-variables)
   - [Static Members](#static-members)
   - [Constants](#constants)
   - [Enumerations](#enumerations)
   - [Local Variables](#local-variables)
   - [Parameters](#parameters)
   - [Global Variables](#global-variables)
   - [Pointers](#pointers)
   - [Type Aliases](#type-aliases)
   - [Namespaces](#namespaces)
   - [Classes and Structs](#classes-and-structs)
   - [Macros](#macros)
   - [File Names](#file-names)
5. [Alternative Naming Conventions](#alternative-naming-conventions)
6. [Code Organization](#code-organization)
7. [Error Handling](#error-handling)
8. [Memory Management](#memory-management)
9. [Thread Safety](#thread-safety)
10. [Documentation Standards](#documentation-standards)
11. [Examples](#examples)
12. [Embedded Systems Best Practices](#embedded-systems-best-practices)
13. [Summary and Quick Reference](#summary-and-quick-reference)

---

## Introduction

This document defines comprehensive coding standards and naming conventions for **embedded systems development** in C and C++. These standards ensure code consistency, maintainability, safety, and alignment with industry best practices for embedded systems.

### About Embedded Systems Development

Embedded systems development has unique requirements:
- **Resource constraints**: Limited memory (RAM/ROM), processing power, and energy
- **Real-time requirements**: Deterministic execution, bounded response times
- **Hardware interaction**: Direct register access, interrupt handling, peripheral control
- **Safety-critical applications**: Automotive (ISO 26262), industrial (IEC 61508), medical devices
- **Long-term maintenance**: Code may be maintained for 10+ years by different engineers
- **Platform diversity**: 8-bit, 16-bit, 32-bit, 64-bit architectures

### Why Coding Standards Matter

In embedded systems, **consistency and clarity are critical**:
- **Safety**: Clear naming prevents bugs that can cause system failures
- **Maintainability**: Consistent patterns reduce cognitive load for engineers
- **Portability**: Standard conventions work across different platforms
- **Collaboration**: Shared conventions enable effective team development
- **Compliance**: Aligns with industry standards (MISRA C/C++, CERT C/C++, AUTOSAR)

---

## Document Purpose and Scope

### Purpose

This document serves as the **single source of truth** for:
- Naming conventions for all code elements
- Code organization and structure guidelines
- Embedded systems best practices
- Error handling patterns
- Documentation requirements

### Scope

**Applies to:**
- All C source files (`.c`, `.h`)
- All C++ source files (`.cpp`, `.hpp`, `.cc`, `.hh`)
- Header files and implementations
- Example code and demos
- Test code and test harnesses
- Documentation comments
- Embedded firmware projects
- Device drivers and HAL implementations
- Real-time systems and safety-critical code

**Does not apply to:**
- Generated code (if any) - though generated code should follow standards when possible
- Third-party libraries (maintain their own conventions)
- Build system files (CMake, Makefiles, etc.) - though consistency is encouraged
- Host/desktop applications (different constraints and requirements)

### Target Audience

- **Primary**: Embedded systems developers writing C/C++ firmware
- **Secondary**: Code reviewers, maintainers, and technical leads
- **Tertiary**: System integrators and hardware engineers
- **Quaternary**: Students and engineers learning embedded systems development

---

## Core Principles

Before diving into specific naming rules, it's essential to understand the **why** behind embedded systems naming conventions. These principles come from **MISRA C/C++**, **CERT C/C++**, **AUTOSAR**, and decades of embedded systems experience:

### 1. Scope Distinction (Critical for Bug Prevention)

**Reasoning**: In embedded systems, confusing a local variable with a member variable, or a parameter with a global, can cause catastrophic bugs. Different naming patterns for different scopes make the code self-documenting and prevent accidental misuse.

**Example Problem**:
```cpp
// ❌ Dangerous: Can't tell scope at a glance
typedef struct {
    bool initialized;
} device_t;

void device_init(device_t* dev) {
    bool initialized = false;  // Shadowing member - BUG!
    // ... later code uses 'initialized' - which one?
}

// ✅ Clear: Scope is obvious (C style)
typedef struct {
    bool initialized_;
} device_t;

void device_init(device_t* dev) {
    bool is_ready = false;  // Local - snake_case, different name
    // No ambiguity possible
}

// ✅ Clear: Scope is obvious (C++ style)
class Device {
    bool initialized_;  // Member - trailing underscore
    void Init() {
        bool is_ready = false;  // Local - snake_case, different name
        // No ambiguity possible
    }
};
```

### 2. Immutability Signaling (Safety-Critical)

**Reasoning**: Constants must be immediately recognizable. Accidentally modifying a constant can cause undefined behavior, timing violations, or safety hazards. UPPER_CASE makes constants stand out visually.

**Example Problem**:
```cpp
// ❌ Dangerous: Looks like a variable
uint16_t maxChannels = 6;
maxChannels = 10;  // Oops! Modified a "constant"

// ✅ Safe: Impossible to miss it's a constant
constexpr uint16_t MAX_CHANNELS_ = 6;
// MAX_CHANNELS_ = 10;  // Compiler error - can't modify constexpr
```

### 3. Type Clarity (Portability & Safety)

**Reasoning**: Embedded code runs on different architectures (8-bit, 16-bit, 32-bit, 64-bit). Using fixed-width types (`uint16_t`) instead of platform-dependent types (`int`) ensures portability. Names should reflect the semantic meaning, not the type (avoid Hungarian notation).

**Example Problem**:
```cpp
// ❌ Platform-dependent
int value;  // Could be 16-bit or 32-bit depending on platform
if (value > 32767) {  // Assumes 16-bit - BUG on 32-bit systems!
}

// ✅ Portable
uint16_t value;  // Always 16-bit, regardless of platform
if (value > 32767) {  // Correct for 16-bit type
}
```

### 4. Self-Documentation (Long-Term Maintainability)

**Reasoning**: Embedded code is often maintained for 10+ years by different engineers. Names must be self-explanatory without comments. Abbreviations that seem obvious today may be unclear in 5 years.

**Example Problem**:
```cpp
// ❌ Unclear: What does 'ch' mean? What is 'curr'?
void set_curr(uint8_t ch, uint16_t curr);
void SetCurr(Channel ch, uint16_t curr);

// ✅ Self-documenting
void set_current_setpoint(uint8_t channel, uint16_t current_ma);
void SetCurrentSetpoint(Channel channel, uint16_t current_ma);
```

### 5. Error-Prone Pattern Avoidance (MISRA Principle)

**Reasoning**: Names that look similar (`l1` vs `I1`, `O0` vs `00`) cause bugs. Embedded systems standards explicitly prohibit confusing names.

**Example Problem**:
```cpp
// ❌ Error-prone: l1 vs I1 vs 11
int l1 = 0;  // lowercase L
int I1 = 0;  // uppercase i
int 11 = 0;  // number eleven
// Easy to confuse in code review or debugging

// ✅ Clear: Distinct names
int channel1_current = 0;
int channel1_index = 0;
int channel11_value = 0;
```

### 6. Hardware Mapping Clarity (Embedded-Specific)

**Reasoning**: When code directly maps to hardware registers or bit fields, names should clearly indicate the hardware connection. This prevents incorrect register access.

**Example Problem**:
```cpp
// ❌ Unclear hardware connection
uint16_t reg1 = 0x0000;
uint16_t bit5 = (1 << 5);

// ✅ Clear hardware mapping
constexpr uint16_t CH_CTRL = 0x0000;  // Channel Control Register
constexpr uint16_t OP_MODE_BIT = (1U << 5);  // Operation Mode bit
```

### 7. Consistency Reduces Cognitive Load (Human Factors)

**Reasoning**: Consistent naming patterns mean engineers spend less mental energy parsing names and more on logic. In safety-critical code, reducing cognitive load reduces bugs.

**Pattern Consistency**:
- All member variables: `name_` (snake_case + trailing underscore)
- All constants: `NAME_` (UPPER_CASE + trailing underscore)
- All local variables: `name` (snake_case, no underscore)
- All parameters: `name` (snake_case, no underscore)
- All public functions: `PascalCase()`
- All private functions: `camelCase()`

This consistency means: 
- "If I see `name_`, it's a member variable"
- "If I see `NAME_`, it's a constant"
- "If I see `name`, it's a local variable or parameter"
- "If I see `PascalCase()`, it's a public function"
- "If I see `camelCase()`, it's a private function"

---

## Naming Conventions

### Functions

#### Public Functions

- **Convention**: PascalCase (first letter capitalized)
  ```cpp
  // C++ examples
  Result<void> Init() noexcept;
  Result<void> EnableChannel(uint8_t channel, bool enabled) noexcept;
  Result<uint16_t> GetAverageCurrent(uint8_t channel) noexcept;
  bool IsInitialized() const noexcept;
  
  // C examples (use snake_case for C)
  int device_init(device_t* dev);
  int enable_channel(device_t* dev, uint8_t channel, bool enabled);
  uint16_t get_average_current(const device_t* dev, uint8_t channel);
  bool is_initialized(const device_t* dev);
  ```

**Reasoning**:
1. **API Clarity**: Public functions are the API - clear, capitalized names signal importance
2. **Distinction**: Different from private functions (camelCase) - clear API boundary
3. **Industry Standard**: PascalCase for public APIs is widely recognized in C++
4. **Self-Documentation**: Capitalized names stand out in code reviews
5. **C vs C++**: C typically uses snake_case for all functions; C++ uses PascalCase for public APIs

**Note**: In **C code**, use **snake_case** for all functions (no distinction between public/private):
```c
// C style: snake_case for all functions
int device_init(device_t* dev);
int enable_channel(device_t* dev, uint8_t channel);
uint16_t read_register(const device_t* dev, uint16_t address);
```

#### Private Functions

- **Convention**: camelCase (first letter lowercase)
  ```cpp
  // C++ private functions
  Result<void> checkInitialized() const noexcept;
  Result<Frame> transferFrame(const Frame& tx_frame) noexcept;
  bool isValidChannelInternal(uint8_t channel) const noexcept;
  
  // C: Use static for "private" functions, snake_case naming
  static int check_initialized(const device_t* dev);
  static int transfer_frame(device_t* dev, const frame_t* tx);
  static bool is_valid_channel(uint8_t channel);
  ```

**Reasoning**:
1. **Scope Distinction**: camelCase distinguishes private from public (PascalCase)
2. **Internal Implementation**: Lowercase signals "internal use only"
3. **Consistency**: Matches modern C++ conventions (Google, LLVM style guides)
4. **Visual Hierarchy**: Public API stands out, private implementation is visually distinct

---

### Member Variables

**Convention: Trailing Underscore (`name_`) + snake_case**

- **All member variables**: Trailing underscore (`_`) + snake_case
  ```cpp
  // C++ member variables
  CommInterface& comm_;                   // Reference member
  bool initialized_;                      // State flag
  bool mission_mode_;                     // Mode flag
  uint16_t channel_enable_cache_;         // Cached value
  std::array<uint16_t, 6> channel_setpoints_; // Array member
  
  // C struct members (no trailing underscore needed, but can be used)
  typedef struct {
      bool initialized_;                  // State flag
      uint8_t channel_count_;            // Counter
      uint16_t* data_buffer_;            // Pointer member
  } device_t;
  ```

**Reasoning for snake_case**:
1. **Readability**: `current_setpoint_ma_` is more readable than `currentSetpointMa_` - each word is clearly separated
2. **Visual Scanning**: Easier to scan and identify word boundaries in multi-word names
3. **Embedded Systems Common Practice**: snake_case is very common in embedded/C codebases
4. **Distinction from Functions**: Variables use snake_case, functions use PascalCase/camelCase - clear separation
5. **C Interoperability**: When interfacing with C code, snake_case is the standard
6. **Long-term Maintainability**: More readable for complex technical terms (e.g., `spi_watchdog_reload_` vs `spiWatchdogReload_`)

**Alternative Convention: Prefix (`m_name`)**

Some codebases use `m_` prefix for member variables:
  ```cpp
  CommInterface& m_comm;                  // Reference member
  bool m_initialized;                     // State flag
  bool m_mission_mode;                    // Mode flag (snake_case with prefix)
  uint16_t m_channel_enable_cache;        // Cached value
  std::array<uint16_t, 6> m_channel_setpoints; // Array member
  ```

**Note**: When using prefixes, the base name can still use snake_case (`m_mission_mode`) or camelCase (`m_missionMode`). Consistency is key.

#### Comparison: Trailing Underscore vs Prefix

| Aspect | Trailing Underscore (`name_`) | Prefix (`m_name`) |
|--------|-------------------------------|-------------------|
| **Modern C++ Style** | ✅ Preferred by Google, LLVM, Boost | ❌ Less common in modern C++ |
| **Visual Clarity** | ✅ Clear, less visual noise | ✅ Very explicit |
| **Readability** | ✅ Natural reading flow | ⚠️ Prefix interrupts flow |
| **MISRA Compliance** | ✅ Acceptable (MISRA doesn't mandate) | ✅ Acceptable (MISRA doesn't mandate) |
| **Embedded Industry** | ✅ Common in modern embedded C++ | ✅ Common in older/legacy code |
| **IDE Support** | ✅ Works well with syntax highlighting | ✅ Works well with syntax highlighting |
| **Refactoring** | ✅ Easy to search/replace | ✅ Easy to search/replace |
| **Hungarian Notation** | ❌ Not Hungarian-style | ⚠️ Similar to Hungarian (discouraged) |
| **Name Length** | ✅ Shorter names | ⚠️ Slightly longer names |

#### Recommendation: **Stick with Trailing Underscore**

**Reasoning**:
1. **Modern C++ Standard**: Aligns with Google C++ Style Guide, LLVM Style Guide, and Boost conventions
2. **Less Visual Noise**: Prefixes add characters that don't contribute to meaning
3. **Natural Reading**: `channel_setpoints_` reads more naturally than `m_channel_setpoints`
4. **MISRA Compliance**: Both are acceptable - MISRA C++ doesn't mandate a specific style, only consistency
5. **Already Established**: Your codebase uses this pattern

**When to Consider Prefixes**:
- Legacy codebase already using prefixes
- Team preference for maximum explicitness
- Integration with existing code that uses prefixes

**Example - Why This Matters**:
```cpp
// ❌ Without underscore: Dangerous shadowing
class Driver {
    bool initialized;
    void Init() {
        bool initialized = checkHardware();  // Shadows member!
        if (initialized) {  // Uses local, not member - BUG!
            this->initialized = true;  // Must use 'this->' to access member
        }
    }
};

// ✅ With underscore: No ambiguity
class Driver {
    bool initialized_;
    void Init() {
        bool is_ready = checkHardware();  // Different name, no conflict
        if (is_ready) {
            initialized_ = true;  // Clear it's the member
        }
    }
};
```

---

### Static Members

**Current Convention: Trailing Underscore (`name_`)**

- **Static member variables**: Trailing underscore (`_`) + `static` keyword
  ```cpp
  class MyClass {
  private:
      static int instance_count_;  // Static member variable
  };
  ```

**Alternative Convention: Prefix (`s_name`)**

Some codebases use `s_` prefix for static members:
  ```cpp
  class MyClass {
  private:
      static int s_instance_count;        // Static member variable (snake_case)
      static int s_instanceCount;         // Static member variable (camelCase)
  };
  ```

#### Comparison: Trailing Underscore vs `s_` Prefix for Statics

| Aspect | Trailing Underscore (`name_`) | Prefix (`s_name`) |
|--------|-------------------------------|-------------------|
| **Distinction from Members** | ⚠️ Same pattern as members | ✅ Explicitly different |
| **Visual Clarity** | ✅ Consistent with members | ✅ Very explicit |
| **Modern C++ Style** | ✅ Preferred | ❌ Less common |
| **Readability** | ✅ Natural flow | ⚠️ Prefix interrupts flow |
| **Consistency** | ✅ Same as member convention | ⚠️ Different from members |

#### Recommendation: **Stick with Trailing Underscore**

**Reasoning**:
1. **Consistency**: Same pattern as regular members - simpler mental model
2. **`static` Keyword is Sufficient**: The `static` keyword already makes it clear
3. **Less Visual Noise**: No need for additional prefix
4. **Modern C++ Standard**: Aligns with major style guides
5. **Already Established**: Your codebase uses this pattern

**When to Use `s_` Prefix**:
- You need to distinguish statics from members at a glance
- Legacy codebase already uses `s_` prefix
- Team preference for maximum explicitness
- When mixing with `m_` prefix convention (members use `m_`, statics use `s_`)

**Note**: Static constants use UPPER_CASE (see Constants section):
  ```cpp
  static constexpr uint16_t MAX_CHANNELS_ = 6;  // Constant, not variable
  ```

---

### Constants

- **Compile-time constants** (`constexpr`): UPPER_CASE with underscores
  ```cpp
  // C++ namespace constants
  namespace Registers {
      constexpr uint16_t CTRL_REG = 0x0000;
      constexpr uint16_t CONFIG_REG = 0x0002;
      constexpr uint16_t MAX_REGISTER_ADDRESS = 0x03FF;
  }
  
  // C++ class-level constants
  class Device {
  public:
      static constexpr uint8_t NUM_CHANNELS_ = 6;
      static constexpr uint16_t DEFAULT_TIMEOUT_MS_ = 1000;
  };
  
  // C constants (use #define or const)
  #define CTRL_REG         0x0000U
  #define CONFIG_REG       0x0002U
  #define MAX_REG_ADDR     0x03FFU
  #define NUM_CHANNELS     6U
  #define DEFAULT_TIMEOUT_MS  1000U
  
  // Or use const for C99+
  static const uint16_t CTRL_REG = 0x0000U;
  static const uint16_t CONFIG_REG = 0x0002U;
  ```

- **Runtime constants** (`const`): Same as compile-time constants
  ```cpp
  const uint32_t SPI_MAX_FREQUENCY = 10'000'000;
  ```

**Reasoning**:
1. **Immutability Signaling**: UPPER_CASE immediately signals "this cannot be modified"
2. **Visual Distinction**: Stands out from variables - prevents accidental modification
3. **Hardware Mapping**: Register addresses and bit masks are constants - clear mapping to hardware
4. **Scope Clarity**: Trailing underscore for class constants distinguishes from namespace constants
5. **MISRA Compliance**: Aligns with MISRA requirement that constants be clearly identifiable
6. **Compile-Time Safety**: `constexpr` ensures compile-time evaluation and prevents runtime modification

**Example - Why UPPER_CASE Matters**:
```cpp
// ❌ Dangerous: Looks like a variable, easy to modify
uint16_t maxChannels = 6;
maxChannels = 10;  // Oops! Modified a "constant"

// ✅ Safe: Impossible to miss it's a constant
constexpr uint16_t MAX_CHANNELS_ = 6;
// MAX_CHANNELS_ = 10;  // Compiler error - can't modify constexpr

// ✅ Hardware register mapping is clear
constexpr uint16_t CH_CTRL = 0x0000;  // Channel Control Register
constexpr uint16_t GLOBAL_CONFIG = 0x0002;  // Global Configuration Register
```

**Naming Patterns for Constants**:
- **Register addresses**: Match datasheet names (`CH_CTRL`, `GLOBAL_CONFIG`)
- **Bit masks**: Descriptive names (`ENABLE_BIT`, `CRC_EN_MASK`)
- **Limits**: Use `MAX_`/`MIN_` prefix (`MAX_CHANNELS_`, `MIN_CURRENT_`)
- **Defaults**: Use `DEFAULT_` prefix (`DEFAULT_WATCHDOG_RELOAD_`)

---

### Enumerations

- **Enum class names (C++)**: PascalCase
  ```cpp
  enum class DeviceError : uint8_t { ... };
  enum class ChannelMode : uint8_t { ... };
  enum class Status : uint8_t { ... };
  ```

- **Enum values (errors/state)**: PascalCase for C++, UPPER_CASE for C
  ```cpp
  // C++ enum class
  enum class DeviceError : uint8_t {
      None = 0,
      NotInitialized,
      HardwareError,
      InvalidChannel
  };
  
  // C enum
  typedef enum {
      DEVICE_ERROR_NONE = 0,
      DEVICE_ERROR_NOT_INITIALIZED,
      DEVICE_ERROR_HARDWARE,
      DEVICE_ERROR_INVALID_CHANNEL
  } device_error_t;
  ```

- **Enum values (flags/bits)**: UPPER_CASE (both C and C++)
  ```cpp
  // C++ enum class for flags
  enum class Status : uint8_t {
      NO_ERROR = 0b00000,
      FRAME_ERROR = 0b00001,
      CRC_ERROR = 0b00010
  };
  
  // C enum for flags
  typedef enum {
      STATUS_NO_ERROR = 0b00000,
      STATUS_FRAME_ERROR = 0b00001,
      STATUS_CRC_ERROR = 0b00010
  } status_flags_t;
  ```

**Reasoning**:
1. **Type Safety**: `enum class` provides type safety and scoping
2. **Error Values**: PascalCase for errors matches modern C++ conventions
3. **Flag Values**: UPPER_CASE for flags matches bit mask conventions
4. **Scope Clarity**: `enum class` prevents namespace pollution

---

### Local Variables

- **Local variables**: snake_case (no trailing underscore)
  ```cpp
  void SomeFunction() {
      uint16_t current_value = 0;
      bool is_enabled = false;
      Channel active_channel = Channel::CH0;
      uint32_t elapsed_time_us = 0;
  }
  ```

**Reasoning**:
1. **Readability**: `current_setpoint_ma` is more readable than `currentSetpointMa` - clear word boundaries
2. **Consistency**: Matches parameter naming (same scope level, same style)
3. **Scope Clarity**: snake_case distinguishes locals from members (`name_`) and constants (`NAME_`)
4. **No Conflicts**: Different from member pattern prevents accidental member access
5. **Embedded Systems Practice**: Common in embedded/C codebases for better readability
6. **Visual Scanning**: Easier to identify word boundaries in technical terms

**Alternative Convention: Prefix (`l_name`)**

Some codebases use `l_` prefix for local variables (less common):
  ```cpp
  void SomeFunction() {
      uint16_t l_current_value = 0;
      bool l_is_enabled = false;
      Channel l_active_channel = Channel::CH0;
  }
  ```

**Note**: Local variable prefixes are rarely used in modern C++ as function scope already provides context. Only use if your codebase already follows this convention.

**Example - Scope Distinction**:
```cpp
void ProcessChannel(Channel channel) {  // Parameter: snake_case
    uint16_t current_value = 0;         // Local: snake_case
    bool is_enabled = false;             // Local: snake_case
    
    // Member access: trailing underscore
    if (initialized_) {                  // Member: name_
        current_value = channel_setpoints_[static_cast<size_t>(channel)];
    }
    
    // Constant access: UPPER_CASE
    if (current_value > MAX_CURRENT_) {  // Constant: NAME_
        current_value = MAX_CURRENT_;
    }
}
```

---

### Parameters

- **Function parameters**: snake_case (no trailing underscore)
  ```cpp
  // C++ parameters
  void SetCurrentSetpoint(Channel channel, uint16_t current_ma, bool parallel_mode);
  Result<void> ConfigureChannel(Channel channel, const ChannelConfig& config);
  Result<void> SetThresholds(uint8_t uv_threshold, uint8_t ov_threshold);
  
  // C parameters
  int set_current_setpoint(device_t* dev, uint8_t channel, uint16_t current_ma);
  int configure_channel(device_t* dev, uint8_t channel, const channel_config_t* config);
  int set_thresholds(device_t* dev, uint8_t uv_threshold, uint8_t ov_threshold);
  ```

**Reasoning**:
1. **Readability**: `current_setpoint_ma` is more readable than `currentSetpointMa` - clear word separation
2. **API Clarity**: Parameters are part of the public API - clear, readable names are essential
3. **Self-Documentation**: Good parameter names reduce need for comments
4. **Units Clarity**: `current_ma` clearly shows "current in milliamperes" vs `currentMa` which is less clear
5. **No Shadowing**: Different from member pattern (`name_`) prevents conflicts
6. **Consistency**: Same style as locals (both are function-scope variables)
7. **Embedded Systems Practice**: Common in embedded/C codebases

**Example - Why Parameter Names Matter**:
```cpp
// ❌ Unclear: What are the units? What do the bools mean?
void configure(uint8_t a, uint8_t b, bool c, bool d);
void Configure(uint8_t a, uint8_t b, bool c, bool d);

// ✅ Self-documenting: Clear purpose and units
void set_voltage_thresholds(
    uint8_t uv_threshold,      // Under-voltage threshold (0.1V per LSB)
    uint8_t ov_threshold,       // Over-voltage threshold (0.1V per LSB)
    bool enable_protection,    // Enable protection circuit
    bool log_events            // Log threshold events
);

// C++ version
void SetVoltageThresholds(
    uint8_t uv_threshold,      // Under-voltage threshold (0.1V per LSB)
    uint8_t ov_threshold,       // Over-voltage threshold (0.1V per LSB)
    bool enable_protection,    // Enable protection circuit
    bool log_events            // Log threshold events
);
```

**Special Cases for Parameters**:
- **Units in name**: When units matter, include them with underscore (`current_ma`, `timeout_us`, `frequency_hz`)
- **Boolean parameters**: Use `is_`/`has_`/`enable_` prefix for clarity (`is_enabled`, `has_fault`, `enable_crc`)
- **Configuration structs**: Use descriptive names (`config`, `settings`, `params`)

**Alternative Convention: Prefix (`a_name` for arguments)**

Some legacy codebases use prefixes for parameters/arguments:
  ```cpp
  // Using a_ prefix for arguments (less common, legacy codebases)
  void SetCurrentSetpoint(Channel a_channel, uint16_t a_current_ma);
  ```

**Note**: 
- Parameter prefixes are **rarely used** in modern C++ as function signatures already provide context
- **`p_` prefix is reserved for pointers** (e.g., `p_ptr`, `p_data`), not parameters
- Only use argument prefixes if your codebase already follows this convention
- Modern practice: Use descriptive names without prefixes for parameters

---

### Global Variables

- **Global variables**: `g_` prefix + snake_case (if globals are necessary)
  ```cpp
  // In global scope (avoid when possible)
  static uint32_t g_transfer_count = 0;        // File-scope global
  extern uint32_t g_error_count;               // External global
  ```

**Reasoning**:
1. **Rarely Needed**: Globals should be avoided in embedded systems - prefer class members or function parameters
2. **Clear Identification**: `g_` prefix immediately signals global scope
3. **Thread Safety**: Globals require synchronization - the prefix reminds developers of this
4. **Scope Clarity**: Distinguishes globals from locals, members, and parameters

**When Globals Might Be Acceptable**:
- Hardware register mappings (use `volatile` and `const` where possible)
- System-wide configuration (prefer singleton or namespace)
- Interrupt handlers (use atomics for thread safety)

**Example**:
```cpp
// Hardware register mapping (acceptable use of global)
volatile uint32_t* const g_spi_base_register = 
    reinterpret_cast<volatile uint32_t*>(0x40000000);

// System configuration (prefer namespace or singleton)
namespace SystemConfig {
    uint32_t g_watchdog_timeout_ms = 1000;  // Configurable system-wide
}
```

**Alternative**: Use namespace or singleton pattern instead of globals:
```cpp
// ✅ Better: Namespace instead of global
namespace SystemState {
    std::atomic<uint32_t> transfer_count{0};
    std::atomic<uint32_t> error_count{0};
}
```

---

### Pointers

- **Pointer variables**: snake_case with descriptive suffix, or `p_` prefix when distinguishing from non-pointers
  ```cpp
  // Preferred: Descriptive name with _ptr suffix
  uint16_t* setpoint_data_ptr = nullptr;
  uint8_t* spi_buffer = nullptr;
  
  // Alternative: p_ prefix (when you need to distinguish pointers)
  uint16_t* p_data = nullptr;
  uint8_t* p_buffer = new uint8_t[256];
  ```

**Reasoning**:
1. **Clarity**: `_ptr` suffix or `p_` prefix makes pointer nature explicit
2. **Safety**: Clear pointer identification helps prevent misuse
3. **Memory Management**: Reminds developers of ownership and cleanup responsibilities
4. **Modern C++**: Prefer smart pointers (`std::unique_ptr`, `std::shared_ptr`) when possible

**Important**: `p_` prefix is for **pointers**, NOT parameters!

**Example**:
```cpp
// ✅ Clear pointer usage
void ProcessData(const uint8_t* data_ptr, size_t length) {
    uint8_t* p_working_buffer = new uint8_t[length];
    // ... use p_working_buffer ...
    delete[] p_working_buffer;
}

// ✅ Better: Use smart pointers
void ProcessData(const uint8_t* data_ptr, size_t length) {
    auto working_buffer = std::make_unique<uint8_t[]>(length);
    // ... automatic cleanup ...
}
```

---

### Type Aliases

- **Type aliases**: PascalCase
  ```cpp
  // C++ type aliases
  using Result = std::expected<T, Error>;
  using CommResult = std::expected<T, CommError>;
  template<typename T>
  using DeviceResult = std::expected<T, DeviceError>;
  
  // C typedefs (use _t suffix convention)
  typedef struct {
      uint8_t status;
      uint16_t data;
  } result_t;
  
  typedef uint32_t device_handle_t;
  typedef void (*callback_t)(uint8_t event);
  ```

**Reasoning**:
1. **Type Clarity**: PascalCase matches class naming - type aliases are types
2. **Consistency**: Matches standard library conventions
3. **Readability**: Clear type names improve code readability

---

### Namespaces

- **Namespace names (C++)**: **lowercase** (preferred for acronym-based names) or PascalCase
  ```cpp
  // ✅ Preferred: lowercase for acronym-based namespaces (avoids conflicts)
  namespace max22200 {
      class MAX22200 { ... };
  }
  
  namespace tle92466ed {
      class TLE92466ED { ... };
  }
  
  namespace ads7952 {
      class ADS7952 { ... };
  }
  
  // ✅ Also acceptable: PascalCase for descriptive namespaces
  namespace Device {
      namespace Registers { ... }
      namespace Config { ... }
  }
  
  // ❌ Avoid: Same case as class name (causes conflicts)
  namespace MAX22200 {  // Conflict with class MAX22200
      class MAX22200 { ... };
  }
  ```
  
  **C Language Note**: C doesn't have namespaces, use prefixes instead:
  ```c
  // device_registers.h
  #define DEV_REG_CTRL      0x0000U
  #define DEV_REG_CONFIG    0x0002U
  
  // Or use structs to group related constants
  typedef struct {
      uint16_t ctrl;
      uint16_t config;
  } device_registers_t;
  ```

**Reasoning**:
1. **Module Organization**: Namespaces group related functionality
2. **Avoid Collisions**: Prevents naming conflicts, especially between namespace and class names
3. **Clear Distinction**: Lowercase namespace + PascalCase class clearly distinguishes namespace from class
4. **Consistency**: Matches pattern used across codebase (max22200/MAX22200, tle92466ed/TLE92466ED, ads7952/ADS7952)
5. **Usage Clarity**: Allows `using namespace max22200;` without ambiguity when class is `MAX22200`

**Best Practice**: When a namespace contains a class with the same base name (especially acronyms), use **lowercase for the namespace** and **PascalCase for the class** to avoid conflicts:
```cpp
// ✅ Good: Clear distinction
namespace max22200 {
    class MAX22200 { ... };
}
using namespace max22200;  // No conflict - can use MAX22200 class

// ❌ Bad: Ambiguous
namespace MAX22200 {
    class MAX22200 { ... };
}
using namespace MAX22200;  // Conflict - MAX22200 refers to both namespace and class
```

---

### Classes and Structs

- **Class names (C++)**: PascalCase
  ```cpp
  class Device { ... };
  class CommInterface { ... };
  struct ChannelConfig { ... };
  struct DeviceStatus { ... };
  
  // For acronyms, use all uppercase (still PascalCase)
  class MAX22200 { ... };
  class TLE92466ED { ... };
  class ADS7952 { ... };
  ```

- **Struct names (C)**: snake_case with `_t` suffix
  ```c
  typedef struct {
      bool initialized;
      uint8_t channel_count;
  } device_t;
  
  typedef struct {
      uint8_t mode;
      uint16_t setpoint;
  } channel_config_t;
  ```

**Reasoning**:
1. **Type Identification**: PascalCase immediately signals "this is a type"
2. **Industry Standard**: Universal C++ convention
3. **Consistency**: Matches standard library and modern C++ conventions
4. **Namespace Distinction**: When namespace is lowercase and class is PascalCase, they are clearly distinguishable (e.g., `max22200::MAX22200`)

**Namespace/Class Naming Pattern**:
When creating a driver library with a namespace and main class, follow this pattern to avoid name conflicts:
```cpp
// ✅ Recommended pattern for acronym-based drivers
namespace max22200 {        // lowercase namespace
    class MAX22200 { ... }; // PascalCase class (all caps for acronyms)
}

// Usage:
using namespace max22200;
MAX22200<MySPI> driver;  // Clear - MAX22200 is the class

// ✅ Also acceptable for descriptive names
namespace Device {         // PascalCase namespace
    class Device { ... };  // PascalCase class (different name or context)
}
```

---

### Macros

- **Macro names**: UPPER_CASE with underscores
  ```cpp
  #define DEVICE_HPP
  #define MAX_RETRY_COUNT 3
  #define ENABLE_DEBUG_LOGGING 1
  ```

**Reasoning**:
1. **Visual Distinction**: UPPER_CASE makes macros stand out (macros are dangerous)
2. **Warning Signal**: UPPER_CASE warns developers to be careful with macros
3. **Industry Standard**: Universal C++ convention
4. **Preprocessor**: Macros are preprocessor constructs - distinct naming is critical

**Note**: Prefer `constexpr` over macros when possible:
```cpp
// ❌ Avoid macros
#define MAX_CHANNELS 6

// ✅ Prefer constexpr
constexpr uint8_t MAX_CHANNELS_ = 6;
```

---

### File Names

**Standard Convention: Lowercase with Underscores**

All source files, header files, and documentation files must use **lowercase with underscores** (`snake_case`):

- **Header files** (`.hpp` for C++, `.h` for C):
  ```cpp
  // C++ headers
  max22200.hpp
  max22200_spi_interface.hpp
  max22200_registers.hpp
  max22200_types.hpp
  
  // C headers
  device.h
  device_hal.h
  device_registers.h
  ```

- **Source files** (`.cpp` for C++, `.c` for C):
  ```cpp
  // C++ sources
  max22200.cpp
  max22200_spi_interface.cpp
  
  // C sources
  device.c
  device_hal.c
  ```

- **Documentation files** (`.md`):
  ```markdown
  // Documentation files
  api_reference.md
  hardware_guide.md
  getting_started.md
  driver_integration_test.md
  ```

- **Example files**:
  ```cpp
  // Example/test files
  max22200_comprehensive_test.cpp
  esp32_max22200_spi.hpp
  driver_integration_test.cpp
  basic_polling_example.cpp
  ```

**Repository Naming Convention: Lowercase with Dashes**

Repository names use **lowercase with dashes** (`kebab-case`):

```bash
# Repository names
hf-max22200-driver
hf-tle92466ed-driver
hf-ads7952-driver
hf-pcal95555-driver
hf-pca9685-driver
hf-ntc-thermistor-driver
hf-ws2812-driver
```

**Key Distinction**:
- **Repository names**: Use **dashes** (`-`) - e.g., `hf-max22200-driver`
- **File names**: Use **underscores** (`_`) - e.g., `max22200_spi_interface.hpp`

**Reasoning**:
1. **Cross-platform compatibility**: Lowercase works on all filesystems (Windows, Linux, macOS)
2. **Case sensitivity**: Avoids issues with case-sensitive vs case-insensitive filesystems
3. **Consistency**: Uniform naming across all files makes navigation easier
4. **Clarity**: Clear relationship between files and their contents
5. **Tool compatibility**: Works well with build systems, version control, and documentation generators
6. **Repository vs File distinction**: Dashes in repository names are URL-friendly; underscores in file names are more readable in code
7. **Documentation consistency**: All documentation files follow the same pattern for easy discovery

---

## Alternative Naming Conventions

This section provides a comprehensive overview of alternative naming conventions used in different codebases. **The current codebase uses trailing underscore convention**, but these alternatives are documented for reference and integration with legacy code.

### Prefix-Based Naming System

Many embedded codebases use prefixes to distinguish variable scope:

| Prefix | Scope | Example | When to Use |
|--------|-------|---------|-------------|
| `m_` | Member variables | `m_initialized`, `m_mission_mode` | Legacy codebases, maximum explicitness |
| `s_` | Static member variables | `s_instance_count`, `s_instanceCount` | When distinguishing statics from members |
| `g_` | Global variables | `g_transfer_count`, `g_error_count` | System-wide variables (avoid when possible) |
| `p_` | **Pointers** | `p_ptr`, `p_data`, `p_buffer` | Pointer variables (not parameters!) |
| `a_` | Arguments/Parameters | `a_channel`, `a_current_ma` | Rarely used, legacy codebases only |
| `l_` | Local variables | `l_current_value`, `l_is_enabled` | Rarely used, legacy codebases |

### Complete Alternative Example

```cpp
// Alternative naming using prefixes (C++)
class Device {
public:
    Result<void> Init() noexcept;
    
private:
    // Member variables: m_ prefix
    CommInterface& m_comm;
    bool m_initialized;
    bool m_operational_mode;
    uint16_t m_channel_enable_cache;
    
    // Static member variables: s_ prefix
    static int s_instance_count;
    static uint32_t s_total_transfers;
    
    // Static constants: UPPER_CASE (no prefix)
    static constexpr uint8_t NUM_CHANNELS_ = 6;
};

// C alternative naming
typedef struct {
    // Member variables: m_ prefix (optional in C)
    bool m_initialized;
    uint8_t m_channel_count;
    uint16_t* m_data_buffer;
} device_t;

// Static variables: s_ prefix
static int s_instance_count = 0;
static uint32_t s_total_transfers = 0;

// Global variables: g_ prefix
uint32_t g_system_error_count = 0;
volatile uint32_t* const g_spi_register = 
    reinterpret_cast<volatile uint32_t*>(0x40000000);

// Function with argument prefix (rare, legacy codebases)
// C++ version
Result<void> Device::SetCurrentSetpoint(
    Channel a_channel,        // Argument: a_ prefix (rare)
    uint16_t a_current_ma)    // Argument: a_ prefix
    noexcept 
{
    uint16_t l_target = 0;   // Local: l_ prefix (rare)
    bool l_is_parallel = false;  // Local: l_ prefix
    
    // Pointer example: p_ prefix (for pointers, not parameters!)
    uint16_t* p_data = nullptr;  // Pointer: p_ prefix
    uint8_t* p_buffer = new uint8_t[256];  // Pointer: p_ prefix
    
    // Member access: m_ prefix
    if (!m_initialized) {
        return std::unexpected(DeviceError::NotInitialized);
    }
    
    // Static access: s_ prefix
    s_total_transfers++;
    
    // Global access: g_ prefix
    if (g_system_error_count > 100) {
        // Handle error
    }
    
    delete[] p_buffer;  // Clean up pointer
    return {};
}

// C version
int device_set_current_setpoint(
    device_t* dev,           // Device handle
    uint8_t a_channel,        // Argument: a_ prefix (rare)
    uint16_t a_current_ma)    // Argument: a_ prefix
{
    uint16_t l_target = 0;   // Local: l_ prefix (rare)
    bool l_is_parallel = false;  // Local: l_ prefix
    
    // Pointer example: p_ prefix (for pointers, not parameters!)
    uint16_t* p_data = NULL;  // Pointer: p_ prefix
    uint8_t* p_buffer = malloc(256);  // Pointer: p_ prefix
    
    // Member access: m_ prefix (or just direct access)
    if (!dev->m_initialized) {
        return DEVICE_ERROR_NOT_INITIALIZED;
    }
    
    // Static access: s_ prefix
    s_total_transfers++;
    
    // Global access: g_ prefix
    if (g_system_error_count > 100) {
        // Handle error
    }
    
    free(p_buffer);  // Clean up pointer
    return 0;
}
```

### Choosing Between Conventions

**Use Trailing Underscore (`name_`) When**:
- Starting a new codebase
- Following modern C++ style guides (Google, LLVM, Boost)
- Wanting less visual noise
- Preferring natural reading flow

**Use Prefixes (`m_`, `s_`, `g_`, `p_` for pointers) When**:
- Integrating with legacy code that uses prefixes
- Team preference for maximum explicitness
- Need to distinguish statics from members at a glance
- Working with codebases that already use prefixes
- **Important**: `p_` prefix is for **pointers**, not parameters!

**Key Principle**: **Consistency is more important than the specific convention**. Choose one style and use it consistently throughout the codebase.

---

## Code Organization

### File Structure

- **One class per file**: Each class should have its own header and implementation file
- **Header guards**: Use `#ifndef` / `#define` / `#endif` or `#pragma once`
- **Include order**: System headers, third-party headers, project headers
- **Forward declarations**: Use forward declarations to reduce compile-time dependencies

### Include Guards

```cpp
#ifndef DEVICE_HPP
#define DEVICE_HPP
// ... code ...
#endif // DEVICE_HPP
```

### Include Order

```cpp
// 1. System headers
#include <expected>
#include <array>
#include <cstdint>

// 2. Third-party headers
#include <esp_idf_version.h>

// 3. Project headers
#include "Device_CommInterface.hpp"
#include "Device_Registers.hpp"
```

### Class Organization

Organize class members in this order:
1. **Public section**: Constructors, destructor, public methods
2. **Protected section**: Protected members (if any)
3. **Private section**: Private methods, member variables

Within each section, group logically:
- Constructors/destructors first
- Core functionality
- Helper methods
- Member variables last

---

## Error Handling

### Error Handling Patterns

#### C++: Use `std::expected` (C++23) or Error Codes

```cpp
// C++23: std::expected
template<typename T>
using Result = std::expected<T, Error>;

// Success case
Result<void> result = {};
if (result) {
    // Success
}

// Error case
Result<uint16_t> value = std::unexpected(Error::NotInitialized);
if (!value) {
    auto error = value.error();
    // Handle error
}

// C++17 and earlier: Use error codes or custom result types
enum class Error {
    None = 0,
    NotInitialized,
    HardwareError
};

struct Result {
    Error error;
    uint16_t value;
};
```

#### C: Use Return Codes

```c
// C: Return error codes
typedef enum {
    DEVICE_OK = 0,
    DEVICE_ERROR_NOT_INITIALIZED,
    DEVICE_ERROR_HARDWARE,
    DEVICE_ERROR_INVALID_PARAM
} device_error_t;

device_error_t device_init(device_t* dev) {
    if (dev == NULL) {
        return DEVICE_ERROR_INVALID_PARAM;
    }
    // ... initialization ...
    return DEVICE_OK;
}
```

### Error Propagation

```cpp
// C++ error propagation
Result<void> SomeFunction() noexcept {
    auto result = AnotherFunction();
    if (!result) {
        return std::unexpected(result.error());  // Propagate error
    }
    return {};
}

// C error propagation
device_error_t some_function(device_t* dev) {
    device_error_t result = another_function(dev);
    if (result != DEVICE_OK) {
        return result;  // Propagate error
    }
    return DEVICE_OK;
}
```

### Error Checking Patterns

```cpp
// C++ Pattern 1: Early return on error
if (auto result = checkInitialized(); !result) {
    return result;  // Return error immediately
}

// C++ Pattern 2: Check and continue
auto result = ReadRegister(address);
if (!result) {
    // Handle error, maybe return
    return std::unexpected(result.error());
}
uint16_t value = *result;  // Use value

// C Pattern 1: Early return on error
device_error_t result = check_initialized(dev);
if (result != DEVICE_OK) {
    return result;  // Return error immediately
}

// C Pattern 2: Check and continue
uint16_t value = 0;
result = read_register(dev, address, &value);
if (result != DEVICE_OK) {
    // Handle error, maybe return
    return result;
}
// Use value
```

---

## Memory Management

### Prefer Stack Allocation

```cpp
// ✅ Preferred: Stack allocation
uint16_t buffer[256];
ChannelConfig config{};

// ❌ Avoid: Heap allocation when not needed
uint16_t* buffer = new uint16_t[256];
```

### Smart Pointers for Dynamic Memory

```cpp
// ✅ Preferred: Smart pointers
auto buffer = std::make_unique<uint8_t[]>(size);
auto shared = std::make_shared<Driver>(comm);

// ❌ Avoid: Raw pointers with manual delete
uint8_t* buffer = new uint8_t[size];
// ... must remember to delete[] ...
```

### RAII Principles

- **Resource Acquisition Is Initialization**: Acquire resources in constructors, release in destructors
- **No manual cleanup**: Let destructors handle cleanup automatically
- **Exception safety**: RAII ensures cleanup even if exceptions occur

---

## Thread Safety

### Current Status

**Embedded drivers and libraries are typically NOT thread-safe by default**. External synchronization is required for multi-threaded access unless explicitly documented as thread-safe.

### Thread Safety Guidelines

1. **Document thread safety**: Clearly document which functions are thread-safe
2. **Use atomics for shared state**: When needed, use `std::atomic` for shared variables
3. **Mutex protection**: Use mutexes for critical sections
4. **Avoid globals**: Prefer thread-local storage or class members

### Example: Thread-Safe Wrapper

```cpp
class ThreadSafeDriver {
public:
    Result<void> Init() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return driver_.Init();
    }
    
private:
    Driver driver_;
    std::mutex mutex_;
};
```

---

## Documentation Standards

### Doxygen Comments

Use Doxygen for all public APIs:

```cpp
/**
 * @brief Brief description
 * 
 * @details Detailed description
 * 
 * @param param_name Parameter description
 * @return Return value description
 * 
 * @pre Precondition description
 * @post Postcondition description
 * 
 * @throws Exception description (if applicable)
 */
Result<void> SomeFunction(uint16_t param) noexcept;
```

### Inline Comments

- **Explain why, not what**: Comments should explain reasoning, not obvious code
- **Use clear language**: Write comments as if explaining to a colleague
- **Keep comments up-to-date**: Update comments when code changes

---

## Examples

### Complete Class Example

```cpp
// C++ Complete Class Example
class Device {
public:
    // Public functions: PascalCase
    Result<void> Init() noexcept;
    Result<void> EnableChannel(uint8_t channel, bool enabled) noexcept;
    Result<uint16_t> GetAverageCurrent(uint8_t channel) noexcept;
    bool IsInitialized() const noexcept;

private:
    // Private functions: camelCase
    Result<void> checkInitialized() const noexcept;
    Result<Frame> transferFrame(const Frame& tx_frame) noexcept;
    
    // Member variables: snake_case + trailing underscore
    CommInterface& comm_;
    bool initialized_;
    bool operational_mode_;
    uint16_t channel_enable_cache_;
    
    // Static constants: UPPER_CASE
    static constexpr uint8_t NUM_CHANNELS_ = 6;
    static constexpr uint16_t DEFAULT_TIMEOUT_MS_ = 1000;
};

// C Complete Struct Example
typedef struct {
    // Member variables: snake_case (trailing underscore optional)
    bool initialized_;
    bool operational_mode_;
    uint16_t channel_enable_cache_;
    uint8_t channel_count_;
} device_t;

// C function declarations
device_error_t device_init(device_t* dev);
device_error_t device_enable_channel(device_t* dev, uint8_t channel, bool enabled);
device_error_t device_get_average_current(const device_t* dev, uint8_t channel, uint16_t* current);
bool device_is_initialized(const device_t* dev);

// C "private" functions (static)
static device_error_t check_initialized(const device_t* dev);
static device_error_t transfer_frame(device_t* dev, const frame_t* tx, frame_t* rx);

// C++ Example function implementation (Current Convention)
Result<void> Device::SetCurrentSetpoint(
    uint8_t channel,           // Parameter: snake_case
    uint16_t current_ma,      // Parameter: snake_case with units
    bool parallel_mode)        // Parameter: snake_case
    noexcept 
{
    uint16_t target_value = 0;  // Local: snake_case
    bool is_parallel = false;   // Local: snake_case
    
    // Member access: snake_case + trailing underscore
    if (!initialized_) {
        return std::unexpected(DeviceError::NotInitialized);
    }
    
    // Constant access: UPPER_CASE
    if (current_ma > MAX_CURRENT_) {
        current_ma = MAX_CURRENT_;
    }
    
    return {};
}

// C Example function implementation
device_error_t device_set_current_setpoint(
    device_t* dev,            // Device handle
    uint8_t channel,           // Parameter: snake_case
    uint16_t current_ma)       // Parameter: snake_case with units
{
    uint16_t target_value = 0;  // Local: snake_case
    bool is_parallel = false;   // Local: snake_case
    
    // Member access: direct struct access
    if (!dev->initialized_) {
        return DEVICE_ERROR_NOT_INITIALIZED;
    }
    
    // Constant access: UPPER_CASE
    if (current_ma > MAX_CURRENT) {
        current_ma = MAX_CURRENT;
    }
    
    return DEVICE_OK;
}

// Alternative: Using prefix convention (legacy codebases)
// C++ version
Result<void> Device::SetCurrentSetpoint(
    uint8_t a_channel,         // Argument: a_ prefix (alternative, rare)
    uint16_t a_current_ma,    // Argument: a_ prefix
    bool a_parallel_mode)      // Argument: a_ prefix
    noexcept 
{
    uint16_t l_target_value = 0;  // Local: l_ prefix (alternative, rare)
    bool l_is_parallel = false;   // Local: l_ prefix
    
    // Pointer example: p_ prefix (for pointers, NOT parameters!)
    uint16_t* p_setpoint_data = nullptr;  // Pointer variable
    
    // Member access: m_ prefix (alternative)
    if (!m_initialized) {
        return std::unexpected(DeviceError::NotInitialized);
    }
    
    // Constant access: UPPER_CASE (same for both conventions)
    if (a_current_ma > MAX_CURRENT_) {
        a_current_ma = MAX_CURRENT_;
    }
    
    return {};
}

// C version with prefixes
device_error_t device_set_current_setpoint(
    device_t* dev,
    uint8_t a_channel,         // Argument: a_ prefix (alternative, rare)
    uint16_t a_current_ma)     // Argument: a_ prefix
{
    uint16_t l_target_value = 0;  // Local: l_ prefix (alternative, rare)
    bool l_is_parallel = false;   // Local: l_ prefix
    
    // Pointer example: p_ prefix (for pointers, NOT parameters!)
    uint16_t* p_setpoint_data = NULL;  // Pointer variable
    
    // Member access: m_ prefix (alternative)
    if (!dev->m_initialized) {
        return DEVICE_ERROR_NOT_INITIALIZED;
    }
    
    // Constant access: UPPER_CASE (same for both conventions)
    if (a_current_ma > MAX_CURRENT) {
        a_current_ma = MAX_CURRENT;
    }
    
    return DEVICE_OK;
}
```

### Constants Examples

```cpp
// C++ namespace constants
namespace Registers {
    // Constants: UPPER_CASE
    constexpr uint16_t CTRL_REG = 0x0000;
    constexpr uint16_t CONFIG_REG = 0x0002;
    constexpr uint16_t MAX_REGISTER_ADDRESS = 0x03FF;
}

// C constants
#define CTRL_REG           0x0000U
#define CONFIG_REG         0x0002U
#define MAX_REGISTER_ADDR  0x03FFU

// Or C99+ const
static const uint16_t CTRL_REG = 0x0000U;
static const uint16_t CONFIG_REG = 0x0002U;
```

---

## Embedded Systems Best Practices

### Fixed-Width Types

Always use fixed-width integer types:

```cpp
// ✅ Correct: Fixed-width types
uint8_t channel;
uint16_t address;
uint32_t data;
int16_t signed_value;

// ❌ Wrong: Platform-dependent types
int channel;        // Could be 16-bit or 32-bit
long address;       // Size varies by platform
```

### Volatile for Hardware Registers

```cpp
// Hardware register access
volatile uint32_t* const SPI_BASE = 
    reinterpret_cast<volatile uint32_t*>(0x40000000);
```

### No Exceptions in Embedded Code

The codebase uses `std::expected` for error handling instead of exceptions:

```cpp
// ✅ Correct: Error handling without exceptions
Result<void> result = SomeFunction();
if (!result) {
    // Handle error
}

// ❌ Wrong: Exceptions in embedded code
try {
    SomeFunction();
} catch (...) {
    // Avoid exceptions in embedded systems
}
```

### Const Correctness

Use `const` wherever possible:

```cpp
// ✅ Correct: Const methods and parameters
bool IsInitialized() const noexcept;
void ProcessData(const uint8_t* data, size_t length) noexcept;

// ✅ Correct: Const member variables
const CommInterface& comm_;
```

### No Dynamic Allocation in Critical Paths

```cpp
// ✅ Preferred: Stack allocation
uint8_t buffer[256];

// ⚠️ Use with caution: Dynamic allocation
auto buffer = std::make_unique<uint8_t[]>(256);
```

### Real-Time Constraints

- **Worst-case execution time (WCET)**: Document and measure WCET
- **Profiling**: Profile code to identify bottlenecks
- **Optimization**: Optimize only after profiling, not prematurely

### Memory Footprint

- **Memory budgets**: Set and track memory budgets per module
- **Code size**: Monitor code size growth
- **RAM usage**: Track stack and heap usage

### Safety Standards Compliance

#### MISRA C/C++

- **Consider MISRA guidelines**: For safety-critical applications
- **Rule compliance**: Document deviations from MISRA rules
- **Tool support**: Use MISRA-compliant static analyzers

#### ISO 26262 (Automotive)

- **ASIL levels**: Determine and document ASIL levels
- **Safety requirements**: Document safety requirements
- **Verification**: Implement verification procedures

#### IEC 61508 (Industrial)

- **SIL levels**: Determine and document SIL levels
- **Safety functions**: Document safety functions
- **Verification**: Implement verification procedures

---

## Summary and Quick Reference

### Consistency Guidelines

1. **Be consistent within the same category**: All member variables use trailing underscore, all constants use UPPER_CASE, etc.

2. **Follow existing patterns**: When adding new code, match the existing naming style in that file/namespace.

3. **Avoid abbreviations**: Prefer `channel` over `ch`, `current` over `curr`, `configuration` over `config` (unless `config` is already established in the codebase).

4. **Use descriptive names**: Names should clearly indicate purpose:
   - ✅ Good: `channel_enable_cache_`, `mission_mode_`, `current_setpoint_ma_`, `checkInitialized()`
   - ❌ Bad: `cache_`, `mode_`, `curr_`, `check()`

5. **Group related items**: Use consistent prefixes/suffixes for related concepts:
   - All register addresses: `Registers::CTRL_REG`, `Registers::CONFIG_REG`
   - All error enums: `DeviceError::None`, `CommError::Timeout`

### Quick Reference Table

| Element | Convention | Example | Alternative |
|---------|-----------|---------|-------------|
| Public functions | PascalCase | `Init()`, `EnableChannel()` | - |
| Private functions | camelCase | `checkInitialized()`, `transferFrame()` | - |
| Member variables | snake_case + `_` suffix | `comm_`, `initialized_`, `mission_mode_` | `m_comm`, `m_initialized`, `m_mission_mode` |
| Static member variables | snake_case + `_` suffix | `instance_count_` | `s_instance_count` |
| Global variables | `g_` prefix + snake_case | `g_transfer_count` | - |
| Constants (`constexpr`/`const`) | UPPER_CASE | `MAX_CHANNELS_`, `CH_CTRL` | - |
| Enum classes | PascalCase | `DriverError`, `ChannelMode` | - |
| Enum values (errors) | PascalCase | `None`, `NotInitialized` | - |
| Enum values (flags) | UPPER_CASE | `NO_ERROR`, `CRC_ERROR` | - |
| Local variables | snake_case | `current_value`, `is_enabled` | `l_current_value` (rare) |
| Parameters | snake_case | `channel`, `current_ma` | `a_channel` (rare, legacy) |
| Pointers | snake_case or `p_` prefix | `data_ptr`, `buffer` | `p_data`, `p_buffer` (when distinguishing pointers) |
| Type aliases | PascalCase | `Result`, `CommResult` | - |
| Classes/Structs | PascalCase | `Device`, `ChannelConfig`, `MAX22200` | - |
| Namespaces | lowercase (preferred) or PascalCase | `max22200`, `tle92466ed`, `Device`, `Registers` | - |
| Macros | UPPER_CASE | `DEVICE_HPP` | - |

### Key Principles Summary

1. **Scope Distinction**: Different naming for different scopes prevents bugs
2. **Immutability Signaling**: UPPER_CASE for constants prevents accidental modification
3. **Type Clarity**: Fixed-width types ensure portability
4. **Self-Documentation**: Clear names reduce need for comments
5. **Error-Prone Pattern Avoidance**: Avoid confusing names
6. **Hardware Mapping Clarity**: Clear hardware register names
7. **Consistency**: Consistent patterns reduce cognitive load

---

## Document Maintenance

### Version History

- **v1.0** (Current): Initial comprehensive coding standards document
  - Established naming conventions
  - Documented alternative conventions
  - Added embedded systems best practices
  - Included examples and quick reference

### Contributing

When updating this document:
1. Maintain consistency with existing patterns
2. Update examples if conventions change
3. Document rationale for any changes
4. Update version history

### Questions or Suggestions

For questions or suggestions about these coding standards, please refer to the project's contribution guidelines or open an issue.

---

**Last Updated**: 2025  
**Document Version**: 1.0  
**Applies To**: Embedded Systems C/C++ Development  
**Language Standards**: C99+, C++11/14/17/20/23
