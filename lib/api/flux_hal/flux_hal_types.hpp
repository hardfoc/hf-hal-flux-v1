/**
 * @file flux_hal_types.hpp
 * @brief Shared HAL types for Flux V1: the manager error catalog and the facade result.
 *
 * ## Purpose
 * Two error lanes, as in pw-hal's coding standards (`coding-standards-managers.md` §2):
 * managers return `bool` and stash a @ref flux::hal::ManagerError (read with
 * `GetManagerError()`); facades return a @ref flux::hal::HalResult. Do not add a third
 * per-manager error enum when @ref flux::hal::ManagerError fits.
 *
 * The older per-manager operation enums (`hf_valve_err_t`, `CommError`, `TemperatureError`,
 * `LedError`, ...) stay on the existing operation APIs and their `GetLastError()`; they are
 * migrated when a manager is next reworked, not in one sweep.
 *
 * ## Ownership
 * Types only. No MCU or RTOS header.
 *
 * Thread safety: value types; no shared state.
 *
 * @author HardFOC Team
 * @date 2026
 */

#ifndef FLUX_HAL_TYPES_HPP_
#define FLUX_HAL_TYPES_HPP_

#include <cstdint>

namespace flux::hal {

/**
 * @brief Lifecycle / bind failure catalog shared by every manager (`GetManagerError()`).
 * @details Set by a manager when `EnsureInitialized()` fails (fail-closed: the manager then
 *          reports `IsInitialized() == false`) or when a later operation finds the hardware
 *          gone. `None` after a successful `EnsureInitialized()`.
 */
enum class ManagerError : uint16_t {
    None = 0,                ///< No error recorded
    NotInitialized = 1,      ///< Manager not initialised (or shut down)
    BindFailed = 2,          ///< A bus, pin or platform object could not be created
    HardwareFault = 3,       ///< A fitted chip did not come up or reported a fault
    InvalidArgument = 4,     ///< Caller passed an invalid argument
    Busy = 5,                ///< Manager busy / re-entrant call refused
    Unsupported = 6,         ///< Operation or part not supported on this build
    DependencyNotReady = 7,  ///< A manager this one needs is not initialised
};

/** @brief Short name of a @ref ManagerError, for logs and diagnostics. */
constexpr const char* ToString(ManagerError e) noexcept {
    switch (e) {
        case ManagerError::None:               return "None";
        case ManagerError::NotInitialized:     return "NotInitialized";
        case ManagerError::BindFailed:         return "BindFailed";
        case ManagerError::HardwareFault:      return "HardwareFault";
        case ManagerError::InvalidArgument:    return "InvalidArgument";
        case ManagerError::Busy:               return "Busy";
        case ManagerError::Unsupported:        return "Unsupported";
        case ManagerError::DependencyNotReady: return "DependencyNotReady";
    }
    return "Unknown";
}

/**
 * @brief Facade operation result.
 * @details `ok == true` and `code == ManagerError::None` on success; otherwise `code` says why
 *          and `detail` may carry the manager's own operation code (for example an
 *          `hf_valve_err_t`) for logs.
 */
struct HalResult {
    bool ok;               ///< true on success
    ManagerError code;     ///< None on success
    uint32_t detail;       ///< Optional manager / driver code (0 if unused)

    /** @brief A success result. */
    static constexpr HalResult Ok() noexcept { return {true, ManagerError::None, 0}; }

    /** @brief A failure result. */
    static constexpr HalResult Err(ManagerError c, uint32_t d = 0) noexcept { return {false, c, d}; }
};

}  // namespace flux::hal

#endif  // FLUX_HAL_TYPES_HPP_
