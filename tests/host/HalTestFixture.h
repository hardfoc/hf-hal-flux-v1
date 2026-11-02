/**
 * @file HalTestFixture.h
 * @brief Shared set-up for the Flux HAL host tests: a fresh board with chip models on its buses.
 *
 * @details The managers are process-wide singletons, so every test case starts by shutting all of
 *          them down and clearing the host binding (@ref ResetHal). @ref Board then installs the
 *          SPI models on the valve and pressure chip selects; a test edits a model (absent part,
 *          fault flags, ADC counts) before the first `EnsureInitialized()`.
 *
 * @copyright HardFOC
 */

#pragma once

#include "AdcManager.h"
#include "CommChannelsManager.h"
#include "FluxChipModels.h"
#include "GpioManager.h"
#include "LedManager.h"
#include "NvsManager.h"
#include "TemperatureManager.h"
#include "ValveManager.h"
#include "api/Flux.h"
#include "bsp_bind/host/FluxHostBind.h"

namespace flux::test {

/** @brief Shut every manager down (reverse init order) and clear every binding slot. */
inline void ResetHal() {
    (void)Flux::GetInstance().Shutdown();
    (void)TemperatureManager::GetInstance().Deinitialize();
    (void)LedManager::GetInstance().Shutdown();
    (void)AdcManager::GetInstance().Deinitialize();
    (void)ValveManager::GetInstance().Deinitialize();
    (void)GpioManager::GetInstance().Deinitialize();
    (void)CommChannelsManager::GetInstance().Deinitialize();
    (void)NvsManager::GetInstance().Deinitialize();
    flux::bsp::host::Reset();
}

/** @brief A Flux V1 board with every chip answering; edit the models before initialising. */
struct Board {
    Max22200Model max;
    Tle92466Model tle;
    Ads7952Model ads;

    Board() {
        ResetHal();
        max.Install();
        tle.Install();
        ads.Install();
    }
    ~Board() { ResetHal(); }
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;
};

/** @brief The MockGpio behind a functional pin, or nullptr if the pin was not created. */
inline hf::host::MockGpio* GpioOf(HfFunctionalGpioPin p) {
    return flux::bsp::host::GpioByPin(Pin(p));
}

}  // namespace flux::test
