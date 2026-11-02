# ===========================================================================
# Flux V1 HAL — host build and manager unit tests (Linux / macOS)
# ===========================================================================
#
# Builds the HAL managers against the host family of the platform binding
# (lib/managers/bsp_bind/host, over hf-core's hf_core_host_mocks) and runs the
# manager tests. No ESP-IDF, no FreeRTOS. On-target builds do not use this file.
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

option(HF_HAL_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(HF_HAL_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
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
