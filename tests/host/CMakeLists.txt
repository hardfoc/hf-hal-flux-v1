# ===========================================================================
# Flux V1 HAL — host build and manager unit tests (Linux / macOS)
# ===========================================================================
#
# Builds the HAL managers against the host family of the platform binding
# (lib/managers/bsp_bind/host, over hf-core's hf_core_host_mocks) and runs the
# manager tests against register-level models of the MAX22200, TLE92466ED and
# ADS7952 (FluxChipModels.h). No ESP-IDF, no FreeRTOS. On-target builds do not
# use this file.
#
#   cmake -S tests/host -B build/host -G Ninja
#   cmake --build build/host
#   ctest --test-dir build/host --output-on-failure
#
# Targets:
#   hf_hal_flux_host   the managers + facade + bsp_bind/host, as a static library
#   test_*             one executable (and one ctest entry) per test file
# ===========================================================================

cmake_minimum_required(VERSION 3.22)
project(hf_hal_flux_host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(HF_HAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)

# Sanitizers, e.g. -DHF_HAL_SANITIZERS=undefined. AddressSanitizer is not usable yet: hf-core's
# MAX22200 SPI adapter copies buffers with aligned 32-bit accesses that touch bytes around them
# (documented in hf-core docs/host-build.md).
set(HF_HAL_SANITIZERS "" CACHE STRING "Comma-separated -fsanitize= list for the host build (empty = none)")
if(HF_HAL_SANITIZERS)
    add_compile_options(-fsanitize=${HF_HAL_SANITIZERS} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${HF_HAL_SANITIZERS})
endif()

# ── hf-core: only the handlers Flux V1 uses ──────────────────────────────
foreach(_feature
        AS5047U ADS9324 AT25040 BNO08X SFM BMP581 SE050 PCA9685 PF1550 PCAL95555
        TMC5160 TMC9660 ISF15ACP4 VORTEX_DRIVER WS2812 DYNAMIXEL CANOPENNODE
        USB_SERIAL_JTAG PWM PIO)
    set(HF_CORE_ENABLE_${_feature} OFF)
endforeach()
foreach(_feature MAX22200 TLE92466ED ADS7952 NTC_THERMISTOR MCP9700 ALICAT_BASIS2 FDO2 LOGGER UART CAN UTILS_CANOPEN)
    set(HF_CORE_ENABLE_${_feature} ON)
endforeach()
set(HF_CORE_BUILD_TESTS OFF)
add_subdirectory("${HF_HAL_ROOT}/lib/core" hf-core)

# ── The HAL under test ────────────────────────────────────────────────────
add_library(hf_hal_flux_host STATIC
    "${HF_HAL_ROOT}/lib/api/Flux.cpp"
    "${HF_HAL_ROOT}/lib/api/flux_hal/flux_hal_valve.cpp"
    "${HF_HAL_ROOT}/lib/managers/NvsManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/CommChannelsManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/GpioManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/AdcManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/ValveManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/LedManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/TemperatureManager.cpp"
    "${HF_HAL_ROOT}/lib/managers/bsp_bind/host/FluxPlatformBindHost.cpp"
)
# lib/ is how the HAL reaches hf-core ("core/hf-core-drivers/..."), whose driver headers are
# separate repositories with their own CI, so it is a SYSTEM directory: their warnings do not fail
# this build. The HAL's own directories stay normal includes and build warning-clean.
target_include_directories(hf_hal_flux_host PUBLIC
    "${HF_HAL_ROOT}/lib/api"
    "${HF_HAL_ROOT}/lib/managers"
)
target_include_directories(hf_hal_flux_host SYSTEM PUBLIC "${HF_HAL_ROOT}/lib")
target_link_libraries(hf_hal_flux_host PUBLIC hf_core_host_mocks)
# Same component flags as the ESP-IDF build (CMakeLists.txt at the repository root).
target_compile_definitions(hf_hal_flux_host PUBLIC
    HARDFOC_HAL=1
    HARDFOC_FLUX_V1=1
    HARDFOC_BOARD_FLUX_V1=1
    HARDFOC_BOARD_SELECTED=1
    HARDFOC_MAX22200_SUPPORT=1
    HARDFOC_TLE92466_SUPPORT=1
    HARDFOC_NTC_THERMISTOR_SUPPORT=1
    HARDFOC_MCP9700_SUPPORT=1
    HARDFOC_ADS7952_SUPPORT=1
    HARDFOC_FLUX_API=1
)
target_compile_options(hf_hal_flux_host PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)

# ── Manager tests ─────────────────────────────────────────────────────────
enable_testing()

function(hf_hal_host_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE hf_hal_flux_host hf_core_host_test_main)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    # Register masks promote to int; the harness compares them with unsigned literals.
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

hf_hal_host_test(test_valve_manager       test_valve_manager.cpp)
hf_hal_host_test(test_adc_manager         test_adc_manager.cpp)
hf_hal_host_test(test_temperature_manager test_temperature_manager.cpp)
hf_hal_host_test(test_nvs_manager         test_nvs_manager.cpp)
hf_hal_host_test(test_flux_hal            test_flux_hal.cpp)
hf_hal_host_test(test_valve_driver_access test_valve_driver_access.cpp)
