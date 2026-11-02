/**
 * @file NvsManager.cpp
 * @brief Flux NVS manager — three `BaseNvs` namespaces from the platform binding.
 */

#include "NvsManager.h"
#include "bsp_bind/FluxPlatformBind.h"

#include "handlers/logger/Logger.h"

static constexpr const char* TAG = "FluxNvs";

//==============================================================================
// SINGLETON
//==============================================================================

NvsManager& NvsManager::GetInstance() noexcept {
    static NvsManager instance;
    return instance;
}

//==============================================================================
// LIFECYCLE
//==============================================================================

NvsManager::NvsManager() noexcept
    : app_(flux::bsp::NvsRegion(0, "flux")),
      calibration_(flux::bsp::NvsRegion(1, "flux_cal")),
      config_(flux::bsp::NvsRegion(2, "flux_cfg")) {}

bool NvsManager::EnsureInitialized() noexcept {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    Logger::GetInstance().Info(TAG, "Initializing Flux NVS (flux / flux_cal / flux_cfg)");

    auto openRegion = [this](BaseNvs& region, const char* label) -> bool {
        if (!region.EnsureInitialized()) {
            SetLastError(hf_nvs_err_t::NVS_ERR_FAILURE);
            Logger::GetInstance().Error(TAG, "NVS init failed for %s (namespace=%s)", label, region.GetNamespace());
            return false;
        }
        Logger::GetInstance().Info(TAG, "NVS ready: %s @ '%s'", label, region.GetNamespace());
        return true;
    };

    if (!openRegion(app_, "app")) {
        return false;
    }
    if (!openRegion(calibration_, "calibration")) {
        (void)app_.EnsureDeinitialized();
        return false;
    }
    if (!openRegion(config_, "config")) {
        (void)calibration_.EnsureDeinitialized();
        (void)app_.EnsureDeinitialized();
        return false;
    }

    SetLastError(hf_nvs_err_t::NVS_SUCCESS);
    initialized_.store(true, std::memory_order_release);
    return true;
}

bool NvsManager::Deinitialize() noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    Logger::GetInstance().Info(TAG, "Deinitializing Flux NVS namespaces");

    (void)config_.EnsureDeinitialized();
    (void)calibration_.EnsureDeinitialized();
    (void)app_.EnsureDeinitialized();

    initialized_.store(false, std::memory_order_release);
    return true;
}

void NvsManager::DumpStatistics() const noexcept {
    Logger::GetInstance().Info(TAG, "NVS: app ns=%s init=%d | cal ns=%s init=%d | cfg ns=%s init=%d",
        app_.GetNamespace(), app_.IsInitialized() ? 1 : 0,
        calibration_.GetNamespace(), calibration_.IsInitialized() ? 1 : 0,
        config_.GetNamespace(), config_.IsInitialized() ? 1 : 0);
}
