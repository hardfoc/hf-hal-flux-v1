/**
 * @file NvsManager.h
 * @brief Non-volatile storage for Flux V1 — three namespaces behind `BaseNvs`.
 *
 * @details Exposes three fixed namespaces for application data, calibration blobs
 *          and device configuration. The `BaseNvs` objects are created by the platform
 *          binding (`bsp_bind/FluxPlatformBind.h`; ESP-IDF NVS on the ESP32-S3), so this
 *          header is MCU-free. Upper layers use `BaseNvs&` from this manager, never the
 *          platform NVS API, so an EEPROM-backed `BaseNvs` can be swapped in without
 *          changing callers.
 *
 * @author HardFOC Team
 * @date 2026
 */

#ifndef FLUX_NVS_MANAGER_H_
#define FLUX_NVS_MANAGER_H_

#include <atomic>
#include <cstdint>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseNvs.h"
#include "flux_hal/flux_hal_types.hpp"

/**
 * @class NvsManager
 * @brief Singleton owning the Flux board's persistent-storage namespaces.
 *
 * Initialization runs **before** communication managers so calibration or
 * board identity can be read early. Namespaces (at most 15 characters):
 *   - `"flux"`      — general application key-value
 *   - `"flux_cal"` — calibration / factory tuning blobs
 *   - `"flux_cfg"` — structured device configuration
 */
class NvsManager {
public:
    static NvsManager& GetInstance() noexcept;

    NvsManager(const NvsManager&) = delete;
    NvsManager& operator=(const NvsManager&) = delete;
    NvsManager(NvsManager&&) = delete;
    NvsManager& operator=(NvsManager&&) = delete;

    /** @brief Initialize NVS flash (once) and open all Flux namespaces. */
    [[nodiscard]] bool EnsureInitialized() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept {
        return initialized_.load(std::memory_order_acquire);
    }

    /** @brief Close namespaces (call after other subsystems if they may persist on shutdown). */
    [[nodiscard]] bool Deinitialize() noexcept;

    /** @brief Last NVS error from the most recent failing operation (any region). */
    [[nodiscard]] hf_nvs_err_t GetLastError() const noexcept { return last_error_; }

    /**
     * @brief Lifecycle / bind error of the last `EnsureInitialized()` (shared catalog).
     * @return `ManagerError::None` when initialised; otherwise why bring-up failed.
     */
    [[nodiscard]] flux::hal::ManagerError GetManagerError() const noexcept {
        return manager_error_.load(std::memory_order_acquire);
    }

    /** @brief General application storage (`flux` namespace). */
    [[nodiscard]] BaseNvs& App() noexcept { return app_; }

    /** @brief Calibration and tuning blobs (`flux_cal` namespace). */
    [[nodiscard]] BaseNvs& Calibration() noexcept { return calibration_; }

    /** @brief Device configuration (`flux_cfg` namespace). */
    [[nodiscard]] BaseNvs& Config() noexcept { return config_; }

    void DumpStatistics() const noexcept;

private:
    NvsManager() noexcept;
    ~NvsManager() = default;

    void SetLastError(hf_nvs_err_t e) noexcept { last_error_ = e; }

    BaseNvs& app_;          ///< `flux`     (binding slot 0)
    BaseNvs& calibration_;  ///< `flux_cal` (binding slot 1)
    BaseNvs& config_;       ///< `flux_cfg` (binding slot 2)

    std::atomic<bool> initialized_{false};
    hf_nvs_err_t last_error_{hf_nvs_err_t::NVS_SUCCESS};
    std::atomic<flux::hal::ManagerError> manager_error_{flux::hal::ManagerError::NotInitialized};
};

#endif // FLUX_NVS_MANAGER_H_
