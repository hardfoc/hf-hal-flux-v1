/**
 * @file flux_hal_valve.cpp
 * @brief ValveBank facade over ValveManager's hardware enable path.
 *
 * @author HardFOC Team
 * @date 2026
 */

#include "flux_hal/flux_hal_valve.hpp"

#include "managers/GpioManager.h"
#include "managers/ValveManager.h"
#include "bsp_bind/board/flux_v1/FluxBoardConfig.h"

namespace flux::hal {

HalResult ValveBank::EnablePins(bool on) noexcept {
    auto& vm = ValveManager::GetInstance();
    if (!on) {
        if (vm.DropEnablePins()) {
            return HalResult::Ok();
        }
        // No pin at all means the GPIO manager is down; a pin that refused the write is a fault.
        bool any = false;
        for (const auto pin : flux::board::kValveEnablePins) {
            any = any || (GpioManager::GetInstance().Get(pin) != nullptr);
        }
        return HalResult::Err(any ? ManagerError::HardwareFault : ManagerError::NotInitialized);
    }
    if (vm.RearmEnablePins()) {
        return HalResult::Ok();
    }
    const ManagerError err = vm.GetManagerError();
    return HalResult::Err(err == ManagerError::None ? ManagerError::HardwareFault : err,
                          static_cast<uint32_t>(vm.GetLastError()));
}

bool ValveBank::EnablePinsHigh() noexcept {
    bool max_en = false;
    bool tle_en = false;
    ValveManager::GetInstance().GetEnablePinStates(max_en, tle_en);
    return max_en && tle_en;
}

bool ValveBank::IsHeldOff() noexcept {
    return ValveManager::GetInstance().IsOutputInhibited();
}

}  // namespace flux::hal
