###############################################################################
#  HardFOC Flux V1 HAL – ESP-IDF single-component build script
###############################################################################

cmake_minimum_required(VERSION 3.16)

set(HF_HAL_ROOT ${CMAKE_CURRENT_LIST_DIR})

#==============================================================================
# EXTRA COMPONENT DIRS
#==============================================================================

list(APPEND EXTRA_COMPONENT_DIRS "${HF_HAL_ROOT}/lib/core/hf-core-drivers/external/")
list(APPEND EXTRA_COMPONENT_DIRS "${HF_HAL_ROOT}/lib/core/hf-core-drivers/external/hf-ws2812-rmt-driver")
list(APPEND EXTRA_COMPONENT_DIRS "${HF_HAL_ROOT}/lib/api")

#==============================================================================
# 1. SOURCE FILE COLLECTION
#==============================================================================

# API Layer
set(API_SOURCES
    "${HF_HAL_ROOT}/lib/api/Flux.cpp"
)

# Managers (6 managers for Flux)
set(MANAGER_SOURCES
    "${HF_HAL_ROOT}/lib/managers/CommChannelsManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/GpioManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/ValveManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/AdcManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/LedManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/TemperatureManager.cpp"
)

# Handlers (from hf-core submodule)
set(HANDLER_SOURCES
    "${HF_HAL_ROOT}/lib/core/handlers/max22200/Max22200Handler.cpp"
    "${HF_HAL_ROOT}/lib/core/handlers/tle92466ed/Tle92466edHandler.cpp"
    "${HF_HAL_ROOT}/lib/core/handlers/ntc/NtcTemperatureHandler.cpp"
    "${HF_HAL_ROOT}/lib/core/handlers/logger/Logger.cpp"
)

# Core Drivers (ESP32 interface wrappers)
set(CORE_DRIVER_SOURCES
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspSpi.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspI2c.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspGpio.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspAdc.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspCan.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspTemperature.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspNvs.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspLogger.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/src/mcu/esp32/EspPeriodicTimer.cpp"
)

# Core Utils
set(CORE_UTILS_SOURCES
    # RTOS wrappers
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/BaseThread.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/CriticalSection.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/FreeRTOSUtils.c"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/Mutex.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/MutexGuard.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/OsUtility.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/PeriodicTimer.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/src/SignalSemaphore.cpp"

    # General utilities
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-general/src/SoftwareVersion.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-general/src/TimestampedVariable.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-general/src/CrcCalculator.c"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-general/src/Utility.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-general/src/VariableAnomalyMonitor.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-general/src/VariableMonitor.cpp"

    # CAN utilities
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-canopen/src/CanOpenUtils.cpp"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-canopen/src/CanOpenMotorUtils.cpp"
)

# All sources
set(HF_HAL_SRCS
    ${API_SOURCES}
    ${MANAGER_SOURCES}
    ${HANDLER_SOURCES}
    ${CORE_DRIVER_SOURCES}
    ${CORE_UTILS_SOURCES}
)

#==============================================================================
# 2. INCLUDE DIRECTORIES
#==============================================================================

set(HF_HAL_INC_DIRS
    # Library root (resolves api/, managers/, handlers/, core/ prefixes)
    "${HF_HAL_ROOT}/lib"

    # API
    "${HF_HAL_ROOT}/lib/api"

    # Managers
    "${HF_HAL_ROOT}/lib/managers"

    # Handlers (via lib/core/handlers/<driver>/)
    "${HF_HAL_ROOT}/lib/core"

    # Core drivers — internal interface wrappers
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/inc"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/internal/hf-pincfg/src"

    # Core utils
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-rtos-wrap/include"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-general/include"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-canopen/include"
    "${HF_HAL_ROOT}/lib/core/hf-core-utils/hf-utils-thread-safe/include"

    # External drivers
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/external/hf-max22200-driver/inc"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/external/hf-tle92466ed-driver/inc"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/external/hf-ntc-thermistor-driver/include"
    "${HF_HAL_ROOT}/lib/core/hf-core-drivers/external/hf-ws2812-rmt-driver/inc"
)

#==============================================================================
# 3. COMPONENT REGISTRATION
#==============================================================================

idf_component_register(
    SRCS          ${HF_HAL_SRCS}
    INCLUDE_DIRS  ${HF_HAL_INC_DIRS}

    REQUIRES
        driver
        freertos
        hf-ws2812-rmt-driver
        nvs_flash
        esp_timer
        esp_system
        esp_common
        esp_rom
        esp_hw_support
        hal
        soc
        log
        esp_adc
        esp_driver_spi
        esp_driver_i2c
        esp_driver_gpio

    PRIV_REQUIRES
        hal
        esp_timer
        esp_adc
        esp_driver_spi
        esp_driver_i2c
        esp_driver_gpio
)

#==============================================================================
# 4. COMPILE OPTIONS
#==============================================================================

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
    -Werror=unused-variable
    -Werror=unused-function
    -Werror=missing-field-initializers
    -Werror=switch
    -Werror=implicit-fallthrough
    -Werror=format
    -Werror=format-security
    -Werror=shadow
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    -DHARDFOC_HAL=1
    -DHARDFOC_FLUX_V1=1
    -DHARDFOC_GPIO_MANAGER=1
    -DHARDFOC_ADC_MANAGER=1
    -DHARDFOC_VALVE_MANAGER=1
    -DHARDFOC_LED_MANAGER=1
    -DHARDFOC_TEMPERATURE_MANAGER=1
    -DHARDFOC_COMM_CHANNELS_MANAGER=1
    -DHARDFOC_MAX22200_SUPPORT=1
    -DHARDFOC_TLE92466_SUPPORT=1
    -DHARDFOC_NTC_THERMISTOR_SUPPORT=1
    -DHARDFOC_WS2812_SUPPORT=1
    -DHARDFOC_PLATFORM_MAPPING=1
    -DHARDFOC_FLUX_API=1
    -DHARDFOC_LOGGER=1
    -DHARDFOC_RTOS_WRAP=1
    -DHARDFOC_CORE_UTILS=1
    -DHARDFOC_CORE_DRIVERS=1
)

set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

#==============================================================================
# 5. BUILD INFORMATION
#==============================================================================

message(STATUS "=== HardFOC Flux V1 HAL Build Configuration ===")
message(STATUS "Root: ${HF_HAL_ROOT}")
message(STATUS "Sources: ${HF_HAL_SRCS}")
message(STATUS "Include dirs: ${HF_HAL_INC_DIRS}")
message(STATUS "=== End Build Configuration ===")
