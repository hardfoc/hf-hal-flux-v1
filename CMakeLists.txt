###############################################################################
#  HardFOC HAL – **single-component** build script (Flux V1)
#
#  Uses core/cmake/hf_core_build_settings.cmake for configurable driver
#  selection. Only Flux-needed drivers are enabled — see Feature Config.
###############################################################################

cmake_minimum_required(VERSION 3.16)

set(HF_HAL_ROOT ${CMAKE_CURRENT_LIST_DIR})

# Include the API directory for Flux API component
list(APPEND EXTRA_COMPONENT_DIRS "${HF_HAL_ROOT}/lib/api")

#==============================================================================
# 1. CORE FEATURE CONFIGURATION — Enable only what Flux needs
#==============================================================================

# ── Platform Selection ────────────────────────────────────────────────────
set(HF_CORE_MCU   "ESP32")              # MCU family (ESP32, STM32 future)
set(HF_CORE_RTOS  "FREERTOS")           # RTOS (FREERTOS, THREADX future)

# ── External Drivers + Handlers ───────────────────────────────────────────
set(HF_CORE_ENABLE_MAX22200       ON)   # SPI high-side switch (valves)
set(HF_CORE_ENABLE_TLE92466ED     ON)   # SPI solenoid driver (valves)
set(HF_CORE_ENABLE_NTC_THERMISTOR ON)   # ADC temperature sensor
set(HF_CORE_ENABLE_MCP9700        ON)   # MCP9700 on ADS7952 (see TemperatureManager)
set(HF_CORE_ENABLE_WS2812         ON)   # RMT LED strip
set(HF_CORE_ENABLE_LOGGER         ON)   # Logging system
set(HF_CORE_ENABLE_CAN            ON)   # CAN/TWAI bus (CommChannelsManager)
set(HF_CORE_ENABLE_ADS7952        ON)   # SPI 12-ch ADC (ADS7952)
set(HF_CORE_ENABLE_ALICAT_BASIS2  ON)   # UART RS-485 mass-flow meter (Alicat BASIS-2)
set(HF_CORE_ENABLE_FDO2           ON)   # UART optical oxygen probe (PyroScience FDO2-G2)
# SE050 secure element (I²C): leave OFF on Flux until a board wires SE050 + enables in product CMake.
set(HF_CORE_ENABLE_SE050          OFF)

# ── Built-in USB Serial/JTAG controller (optional console transport) ─────
# OFF by default: Flux V1 ships with an external USB-UART bridge wired to
# UART0 (GPIO43/44), so the IDF bootloader/log/operator-console all share
# that port the way the chip ROM expects. Flip this to ON only when you
# want the operator CLI to ride on the ESP32-S3's built-in USB Serial/JTAG
# controller (GPIO19/20, native USB jack) instead — that pulls in
# `EspUsbSerialJtag` + the IDF `esp_driver_usb_serial_jtag` component and
# wakes up `CommChannelsManager::AcquireUsbSerialJtag()`. The HAL stack
# stays compiled out (zero code, zero deps) when this is OFF.
set(HF_CORE_ENABLE_USB_SERIAL_JTAG OFF)

# ── Optional Utilities ────────────────────────────────────────────────────
set(HF_CORE_ENABLE_UTILS_CANOPEN  ON)   # CANopen protocol helpers

# ── Include core build settings (collects sources, includes, IDF deps) ───
include("${HF_HAL_ROOT}/lib/core/cmake/hf_core_build_settings.cmake")

#==============================================================================
# 2. HAL-SPECIFIC SOURCES
#==============================================================================

# API Layer
set(API_SOURCES
    "${HF_HAL_ROOT}/lib/api/Flux.cpp"
    "${HF_HAL_ROOT}/lib/api/FluxVersionManifest.cpp"
)

# Manager Sources (7 managers)
set(MANAGER_SOURCES
    "${HF_HAL_ROOT}/lib/managers/NvsManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/CommChannelsManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/GpioManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/AdcManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/ValveManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/LedManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/TemperatureManager.cpp"
)

# Platform binding (lib/managers/bsp_bind): the only HAL sources allowed to use
# ESP-IDF, FreeRTOS or Esp* types. Exactly one MCU family is linked; the host
# test build (tests/host) links bsp_bind/host instead.
if(HF_CORE_MCU STREQUAL "ESP32")
    set(BSP_BIND_SOURCES
        "${HF_HAL_ROOT}/lib/managers/bsp_bind/esp32s3/FluxPlatformBindEsp32s3.cpp"
    )
else()
    message(FATAL_ERROR "[hf-hal-flux] No bsp_bind family for HF_CORE_MCU=${HF_CORE_MCU}")
endif()

# Combine HAL + core sources
set(HF_HAL_SRCS
    ${API_SOURCES}
    ${MANAGER_SOURCES}
    ${BSP_BIND_SOURCES}
    ${HF_CORE_SOURCES}
)

#==============================================================================
# 3. INCLUDE DIRECTORIES
#==============================================================================

set(HF_HAL_INC_DIRS
    # HAL-specific
    "${HF_HAL_ROOT}/lib"
    "${HF_HAL_ROOT}/lib/api"
    "${HF_HAL_ROOT}/lib/managers"
    # Core (handlers, drivers, utils, interfaces)
    ${HF_CORE_INCLUDE_DIRS}
)

#==============================================================================
# 4. COMPONENT REGISTRATION
#==============================================================================

if(COMMAND idf_component_register)
    # ESP-IDF build — register as an IDF component
    idf_component_register(
        SRCS          ${HF_HAL_SRCS}
        INCLUDE_DIRS  ${HF_HAL_INC_DIRS}
        REQUIRES      ${HF_CORE_IDF_REQUIRES}
    )
else()
    # Non-IDF build (host testing, CI, standalone CMake)
    add_library(hf-hal-flux STATIC ${HF_HAL_SRCS})
    target_include_directories(hf-hal-flux PUBLIC ${HF_HAL_INC_DIRS})
    set(COMPONENT_LIB hf-hal-flux)
endif()

#==============================================================================
# 5. COMPILE OPTIONS AND DEFINITIONS
#==============================================================================

# Strict compile options for HAL code quality
# Note: -Werror=padded, -Werror=suggest-*, -Werror=unused-function removed
# because they fire on core/third-party struct layouts, header-only static
# functions, and virtual method patterns the HAL cannot control.
# -Wno-padded: same rationale — otherwise ~1k+ lines of struct-padding noise
#   per full build (IDF, FreeRTOS, static buffers).
# C-only and C++-only -Werror= must be set per language: applying C flags to
# cc1plus and C++ flags to cc1 each emits a "not valid for …" driver warning
# for every .c / .cpp file.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-error=undef
    -Werror=null-dereference
    -Werror=nonnull
    -Werror=nonnull-compare
    -Werror=address
    -Werror=alloca
    -Werror=builtin-macro-redefined
    -Werror=div-by-zero
    -Wno-error=duplicated-cond
    -Werror=empty-body
    -Werror=enum-compare
    -Werror=enum-conversion
    -Werror=free-nonheap-object
    -Werror=ignored-qualifiers
    -Werror=init-self
    -Werror=logical-not-parentheses
    -Werror=logical-op
    -Werror=main
    -Werror=maybe-uninitialized
    -Werror=memset-transposed-args
    -Werror=mismatched-dealloc
    -Wno-error=missing-include-dirs
    -Werror=multichar
    -Werror=multistatement-macros
    -Werror=narrowing
    -Wno-error=noexcept
    -Werror=parentheses
    -Werror=pointer-arith
    -Werror=return-local-addr
    -Werror=sequence-point
    -Werror=shift-count-negative
    -Werror=shift-count-overflow
    -Werror=shift-negative-value
    -Werror=shift-overflow
    -Werror=shift-overflow=2
    -Werror=switch-bool
    -Werror=tautological-compare
    -Werror=trigraphs
    -Werror=type-limits
    -Werror=uninitialized
    -Werror=unknown-pragmas
    -Werror=unreachable-code
    -Werror=varargs
    -Werror=variadic-macros
    -Wno-error=vla
    -Werror=volatile-register-var
    -Werror=write-strings
    -Wno-error=zero-as-null-pointer-constant
    -Wno-error=format=
    -Wno-padded
    -Wunused-function
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-duplicated-branches
    -Wno-missing-braces
    -Wno-missing-declarations
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit>
    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
    $<$<COMPILE_LANGUAGE:C>:-Werror=pointer-sign>
    $<$<COMPILE_LANGUAGE:CXX>:-Werror=mismatched-new-delete>
    $<$<COMPILE_LANGUAGE:CXX>:-Werror=non-template-friend>
    $<$<COMPILE_LANGUAGE:CXX>:-Werror=reorder>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-volatile>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual>
)

# WS2812 Kconfig defaults. Only the ESP32-S3 binding includes the ws2812
# headers now; the definitions stay PUBLIC so an application that includes the
# driver directly sees the same values.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CONFIG_WS2812_NUM_LEDS=1
    CONFIG_WS2812_LED_TYPE_RGB=1
    CONFIG_WS2812_T0H=350
    CONFIG_WS2812_T1H=900
    CONFIG_WS2812_T0L=900
    CONFIG_WS2812_T1L=350
    CONFIG_WS2812_DEFAULT_BRIGHTNESS=25
    CONFIG_WS2812_LED_RMT_TX_GPIO=48
    CONFIG_WS2812_LED_RMT_TX_CHANNEL=0
)

# Core feature definitions — PUBLIC because downstream code (e.g. main/)
# includes core headers that use these in preprocessor guards.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ${HF_CORE_COMPILE_DEFINITIONS}
)

# HAL-specific compile definitions (PRIVATE to this component)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # HAL identity + board selection (drives pin config selector in core/)
    HARDFOC_HAL=1
    HARDFOC_FLUX_V1=1
    HARDFOC_BOARD_FLUX_V1=1
    HARDFOC_BOARD_SELECTED=1
    # HAL component flags
    HARDFOC_COMPONENT_HANDLER=1
    HARDFOC_GPIO_MANAGER=1
    HARDFOC_ADC_MANAGER=1
    HARDFOC_VALVE_MANAGER=1
    HARDFOC_LED_MANAGER=1
    HARDFOC_TEMPERATURE_MANAGER=1
    HARDFOC_COMM_CHANNELS_MANAGER=1
    HARDFOC_MAX22200_SUPPORT=1
    HARDFOC_TLE92466_SUPPORT=1
    HARDFOC_NTC_THERMISTOR_SUPPORT=1
    HARDFOC_MCP9700_SUPPORT=1
    HARDFOC_WS2812_SUPPORT=1
    HARDFOC_ADS7952_SUPPORT=1
    HARDFOC_PLATFORM_MAPPING=1
    HARDFOC_FLUX_API=1
    HARDFOC_DRIVER_HANDLERS=1
)

# MCU target auto-detection from IDF sdkconfig (required by McuSelect.h)
if(CONFIG_IDF_TARGET_ESP32)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HF_TARGET_MCU_ESP32=1 HF_MCU_ESP32=1 HF_MCU_FAMILY_ESP32=1 HF_THREAD_SAFE=1 ESP32=1)
elseif(CONFIG_IDF_TARGET_ESP32S3)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HF_TARGET_MCU_ESP32S3=1 HF_MCU_ESP32S3=1 HF_MCU_FAMILY_ESP32=1 HF_THREAD_SAFE=1 ESP32S3=1)
elseif(CONFIG_IDF_TARGET_ESP32C6)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HF_TARGET_MCU_ESP32C6=1 HF_MCU_ESP32C6=1 HF_MCU_FAMILY_ESP32=1 HF_THREAD_SAFE=1 ESP32C6=1)
elseif(CONFIG_IDF_TARGET_ESP32S2)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HF_TARGET_MCU_ESP32S2=1 HF_MCU_ESP32S2=1 HF_MCU_FAMILY_ESP32=1 HF_THREAD_SAFE=1 ESP32S2=1)
elseif(CONFIG_IDF_TARGET_ESP32C3)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HF_TARGET_MCU_ESP32C3=1 HF_MCU_ESP32C3=1 HF_MCU_FAMILY_ESP32=1 HF_THREAD_SAFE=1 ESP32C3=1)
elseif(CONFIG_IDF_TARGET_ESP32H2)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HF_TARGET_MCU_ESP32H2=1 HF_MCU_ESP32H2=1 HF_MCU_FAMILY_ESP32=1 HF_THREAD_SAFE=1 ESP32H2=1)
else()
    message(WARNING "[hf-hal-flux] Unknown IDF target — McuSelect.h may fail. Set HF_TARGET_MCU_* manually.")
endif()

# GNU++20 for Flux HAL (GNU extensions required because ESP-IDF HAL headers
# use typeof/statement-expressions that require -std=gnu++20 not -std=c++20)
set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

#==============================================================================
# 6. BUILD SUMMARY
#==============================================================================

list(LENGTH HF_HAL_SRCS _hal_total_srcs)
list(LENGTH API_SOURCES _hal_api_count)
list(LENGTH MANAGER_SOURCES _hal_mgr_count)
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════╗")
message(STATUS "║          Flux V1 HAL Build Configuration            ║")
message(STATUS "╚══════════════════════════════════════════════════════╝")
message(STATUS "  HAL sources:    ${_hal_api_count} API + ${_hal_mgr_count} managers")
message(STATUS "  Total sources:  ${_hal_total_srcs}")
message(STATUS "  Component:      ${COMPONENT_LIB}")
message(STATUS "")

#==============================================================================
# 7. HAL VERSION MANIFEST — git SHAs of every HAL-owned submodule
#
# The HAL owns its inventory: CMake captures the actual checked-out HEAD
# short SHA of every submodule the HAL pulls in, writes them into a
# generated header, and the FluxVersionManifest.cpp source consumes them
# alongside each driver's HF_*_VERSION_STRING macros to publish a stable
# read-only `flux_version::GetHalManifest()` view to higher layers.
#==============================================================================
set(_HF_HAL_VER_DIR "${CMAKE_CURRENT_BINARY_DIR}/flux_hal_version_generated")
file(MAKE_DIRECTORY "${_HF_HAL_VER_DIR}")

find_package(Git QUIET)

# Resolve a repo's checked-out HEAD short SHA. Works for both the parent
# repo and any submodule (each submodule has its own .git linkfile).
function(_hf_hal_repo_sha repo out)
    set(${out} "unknown" PARENT_SCOPE)
    if(NOT GIT_FOUND)
        return()
    endif()
    if(NOT IS_DIRECTORY "${repo}/.git" AND NOT EXISTS "${repo}/.git")
        return()
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C "${repo}" rev-parse --short=7 HEAD
        OUTPUT_VARIABLE _sha
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc)
    if(_rc EQUAL 0 AND NOT _sha STREQUAL "")
        set(${out} "${_sha}" PARENT_SCOPE)
    endif()
endfunction()

set(_HF_HAL_DRV "${HF_HAL_ROOT}/lib/core/hf-core-drivers")
set(_HF_HAL_UTL "${HF_HAL_ROOT}/lib/core/hf-core-utils")

_hf_hal_repo_sha("${HF_HAL_ROOT}"                                   _HF_HAL_SHA_HAL)
_hf_hal_repo_sha("${HF_HAL_ROOT}/lib/core"                          _HF_HAL_SHA_HF_CORE)
_hf_hal_repo_sha("${_HF_HAL_DRV}"                                   _HF_HAL_SHA_HF_CORE_DRIVERS)
_hf_hal_repo_sha("${_HF_HAL_UTL}"                                   _HF_HAL_SHA_HF_CORE_UTILS)

_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-ads7952-driver"        _HF_HAL_SHA_ADS7952)
_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-alicat-basis2-driver"  _HF_HAL_SHA_ALICAT_BASIS2)
_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-fdo2-driver"           _HF_HAL_SHA_FDO2)
_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-max22200-driver"       _HF_HAL_SHA_MAX22200)
_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-mcp9700-driver"        _HF_HAL_SHA_MCP9700)
_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-ntc-thermistor-driver" _HF_HAL_SHA_NTC_THERMISTOR)
_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-tle92466ed-driver"     _HF_HAL_SHA_TLE92466ED)
_hf_hal_repo_sha("${_HF_HAL_DRV}/external/hf-ws2812-rmt-driver"     _HF_HAL_SHA_WS2812_RMT)
_hf_hal_repo_sha("${_HF_HAL_DRV}/internal/hf-internal-interface-wrap" _HF_HAL_SHA_IIWRAP)
_hf_hal_repo_sha("${_HF_HAL_DRV}/internal/hf-pincfg"                _HF_HAL_SHA_PINCFG)

_hf_hal_repo_sha("${_HF_HAL_UTL}/hf-utils-canopen"   _HF_HAL_SHA_UTIL_CANOPEN)
_hf_hal_repo_sha("${_HF_HAL_UTL}/hf-utils-general"   _HF_HAL_SHA_UTIL_GENERAL)
_hf_hal_repo_sha("${_HF_HAL_UTL}/hf-utils-rtos-wrap" _HF_HAL_SHA_UTIL_RTOS)

string(TIMESTAMP _HF_HAL_BUILD_STAMP "%Y-%m-%d %H:%M:%S UTC" UTC)

# Write to a .tmp then copy_if_different so unchanged SHAs do NOT cause
# unnecessary rebuilds of FluxVersionManifest.cpp every configure.
file(WRITE "${_HF_HAL_VER_DIR}/flux_hal_git_manifest.h.tmp"
"// Auto-generated by hf-hal-flux-v1/CMakeLists.txt — DO NOT EDIT.
// Captures the actual checked-out HEAD short SHA of every HAL-owned
// repo at HAL configure time.
#pragma once

#define HF_HAL_BUILD_STAMP             \"${_HF_HAL_BUILD_STAMP}\"

#define HF_HAL_GIT_SHA_HAL             \"${_HF_HAL_SHA_HAL}\"
#define HF_HAL_GIT_SHA_HF_CORE         \"${_HF_HAL_SHA_HF_CORE}\"
#define HF_HAL_GIT_SHA_HF_CORE_DRIVERS \"${_HF_HAL_SHA_HF_CORE_DRIVERS}\"
#define HF_HAL_GIT_SHA_HF_CORE_UTILS   \"${_HF_HAL_SHA_HF_CORE_UTILS}\"

#define HF_HAL_GIT_SHA_ADS7952         \"${_HF_HAL_SHA_ADS7952}\"
#define HF_HAL_GIT_SHA_ALICAT_BASIS2   \"${_HF_HAL_SHA_ALICAT_BASIS2}\"
#define HF_HAL_GIT_SHA_FDO2            \"${_HF_HAL_SHA_FDO2}\"
#define HF_HAL_GIT_SHA_MAX22200        \"${_HF_HAL_SHA_MAX22200}\"
#define HF_HAL_GIT_SHA_MCP9700         \"${_HF_HAL_SHA_MCP9700}\"
#define HF_HAL_GIT_SHA_NTC_THERMISTOR  \"${_HF_HAL_SHA_NTC_THERMISTOR}\"
#define HF_HAL_GIT_SHA_TLE92466ED      \"${_HF_HAL_SHA_TLE92466ED}\"
#define HF_HAL_GIT_SHA_WS2812_RMT      \"${_HF_HAL_SHA_WS2812_RMT}\"

#define HF_HAL_GIT_SHA_IIWRAP          \"${_HF_HAL_SHA_IIWRAP}\"
#define HF_HAL_GIT_SHA_PINCFG          \"${_HF_HAL_SHA_PINCFG}\"

#define HF_HAL_GIT_SHA_UTIL_CANOPEN    \"${_HF_HAL_SHA_UTIL_CANOPEN}\"
#define HF_HAL_GIT_SHA_UTIL_GENERAL    \"${_HF_HAL_SHA_UTIL_GENERAL}\"
#define HF_HAL_GIT_SHA_UTIL_RTOS       \"${_HF_HAL_SHA_UTIL_RTOS}\"
")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${_HF_HAL_VER_DIR}/flux_hal_git_manifest.h.tmp"
    "${_HF_HAL_VER_DIR}/flux_hal_git_manifest.h")

# The generated header is consumed by FluxVersionManifest.cpp only —
# PRIVATE include is sufficient.
target_include_directories(${COMPONENT_LIB} PRIVATE "${_HF_HAL_VER_DIR}")
