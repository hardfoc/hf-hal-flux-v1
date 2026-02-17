/**
 * @file HfValveError.h
 * @ingroup valve
 * @brief Unified valve error codes for the HardFOC Flux V1 platform.
 *
 * @details
 * Provides a comprehensive error enumeration for all valve operations in the
 * Flux V1 system. Follows the X-macro pattern established by BaseGpio, BaseSpi,
 * and BaseAdc for consistent error handling across the HardFOC ecosystem.
 *
 * This error type captures errors from two driver ICs (MAX22200 and TLE92466ED)
 * plus handler-level and manager-level errors, presenting them through a single
 * unified enum. Translation functions from driver-native error types are
 * provided in HfValveErrorMapping.h.
 *
 * @author HardFOC Team
 * @date 2026
 * @copyright HardFOC
 */

#pragma once

#include <cstdint>
#include <string_view>

/**
 * @defgroup valve Valve Module
 * @brief All valve-related types, enums, and functions for solenoid and proportional valve control.
 */

// ─── X-MACRO Error Table ────────────────────────────────────────────────────
/**
 * @ingroup valve
 * @brief HardFOC valve error codes macro list
 * @details X-macro pattern for comprehensive error enumeration. Each entry contains:
 *          X(NAME, VALUE, DESCRIPTION)
 */
#define HF_VALVE_ERR_LIST(X)                                                     \
  /* Success */                                                                  \
  X(VALVE_SUCCESS,                    0,  "Success")                             \
                                                                                 \
  /* General errors */                                                           \
  X(VALVE_ERR_NOT_INITIALIZED,        1,  "Manager not initialized")             \
  X(VALVE_ERR_DRIVER_NOT_AVAILABLE,   2,  "Driver IC not available")             \
  X(VALVE_ERR_INVALID_CHANNEL,        3,  "Invalid channel for operation")       \
  X(VALVE_ERR_CHANNEL_TYPE_MISMATCH,  4,  "Channel type mismatch (sol/prop)")    \
  X(VALVE_ERR_INVALID_PARAMETER,      5,  "Invalid parameter value")             \
  X(VALVE_ERR_NULL_POINTER,           6,  "Null handler/driver pointer")         \
                                                                                 \
  /* Communication errors (SPI layer) */                                         \
  X(VALVE_ERR_SPI_BUS_ERROR,          10, "SPI bus communication error")         \
  X(VALVE_ERR_SPI_TIMEOUT,            11, "SPI transfer timeout")               \
  X(VALVE_ERR_SPI_TRANSFER_FAILED,    12, "SPI data transfer failed")            \
  X(VALVE_ERR_SPI_CRC_ERROR,          13, "SPI CRC mismatch")                   \
  X(VALVE_ERR_SPI_FRAME_ERROR,        14, "SPI frame error from device")         \
                                                                                 \
  /* Driver-level errors */                                                      \
  X(VALVE_ERR_HARDWARE_FAULT,         20, "Hardware fault detected")             \
  X(VALVE_ERR_DEVICE_NOT_RESPONDING,  21, "Device not responding")               \
  X(VALVE_ERR_WRONG_DEVICE_ID,        22, "Wrong device ID")                     \
  X(VALVE_ERR_REGISTER_ERROR,         23, "Register read/write error")           \
  X(VALVE_ERR_CONFIGURATION_ERROR,    24, "Configuration failed")                \
  X(VALVE_ERR_WRONG_MODE,             25, "Operation not allowed in mode")       \
  X(VALVE_ERR_WRITE_TO_READ_ONLY,     26, "Write to read-only register")         \
  X(VALVE_ERR_TIMEOUT,                27, "Operation timeout")                   \
                                                                                 \
  /* Manager-level errors */                                                     \
  X(VALVE_ERR_MUTEX_TIMEOUT,          30, "Mutex acquisition timeout")           \
  X(VALVE_ERR_DEPENDENCY_NOT_READY,   31, "Required dependency not ready")       \
                                                                                 \
  /* Catch-all */                                                                \
  X(VALVE_ERR_UNKNOWN,                255, "Unknown error")

// ─── Enum generation ────────────────────────────────────────────────────────
/**
 * @ingroup valve
 * @brief HardFOC valve error codes
 * @details Comprehensive error enumeration for all valve operations in the system.
 */
enum class hf_valve_err_t : uint8_t {
#define X(NAME, VALUE, DESC) NAME = VALUE,
  HF_VALVE_ERR_LIST(X)
#undef X
};

// ─── ToString (human-readable description) ──────────────────────────────────
/**
 * @ingroup valve
 * @brief Convert hf_valve_err_t to human-readable description string
 * @param err The error code to convert
 * @return String view of the error description
 */
constexpr std::string_view HfValveErrToString(hf_valve_err_t err) noexcept {
  switch (err) {
#define X(NAME, VALUE, DESC)                                                     \
    case hf_valve_err_t::NAME: return DESC;
    HF_VALVE_ERR_LIST(X)
#undef X
    default: return "Unknown valve error";
  }
}

// ─── Name (enum name as string) ─────────────────────────────────────────────
/**
 * @ingroup valve
 * @brief Convert hf_valve_err_t to its enum name as a C-string
 * @param err The error code to convert
 * @return Non-null C string of the enum identifier
 */
constexpr const char* HfValveErrName(hf_valve_err_t err) noexcept {
  switch (err) {
#define X(NAME, VALUE, DESC)                                                     \
    case hf_valve_err_t::NAME: return #NAME;
    HF_VALVE_ERR_LIST(X)
#undef X
    default: return "VALVE_ERR_UNKNOWN";
  }
}

// ─── Convenience ────────────────────────────────────────────────────────────
/**
 * @ingroup valve
 * @brief Check if a valve error indicates success
 * @param err The error code to check
 * @return true if err == VALVE_SUCCESS
 */
constexpr bool HfValveErrOk(hf_valve_err_t err) noexcept {
  return err == hf_valve_err_t::VALVE_SUCCESS;
}
