/**
 * @file WhValveControlThread.h
 * @brief BaseThread that pushes pending valve setpoints to the driver ICs.
 *
 * @details Runs on its own RTOS task at a fixed period (default 10 ms / 100 Hz).
 *          Each iteration walks every valve in the WhValveSystem; for any valve
 *          whose dirty flag is set, it calls `Apply(ValveManager&)` so the
 *          new setpoint reaches the hardware.
 *
 *          The thread is intentionally lean: it does NOT poll fault registers,
 *          it does NOT read back diagnostics — that is the dedicated
 *          diagnostics thread's job. This separation makes it cheap to call
 *          `RequestPosition` from a 500 Hz control loop without ever stalling
 *          on SPI register reads.
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "BaseThread.h"
#include "WhValveSystem.h"
#include "managers/ValveManager.h"

#include <atomic>
#include <cstdint>

namespace whvalve {

class WhValveControlThread final : public BaseThread {
public:
    /**
     * @param system           The shared WhValveSystem instance.
     * @param manager          Flux::ValveManager (need not be initialised yet —
     *                         the thread surfaces NOT_INITIALIZED until ready).
     * @param sample_period_ms Step period in ms. 10 ms = 100 Hz default;
     *                         feel free to drop to 5 ms or raise to 50 ms.
     */
    WhValveControlThread(WhValveSystem& system,
                         ValveManager&  manager,
                         uint32_t       sample_period_ms = 10) noexcept
        : BaseThread("WhValveCtrl"),
          system_(system),
          manager_(manager),
          sample_period_ms_(sample_period_ms) {}

    void SetSamplePeriodMs(uint32_t ms) noexcept {
        if (ms == 0) ms = 1;
        sample_period_ms_ = ms;
    }

    /// True once the thread has at least once observed manager.IsInitialized().
    bool ManagerWasReady() const noexcept {
        return mgr_was_ready_.load(std::memory_order_acquire);
    }

    /// Total full sweep count (one per Step()).
    uint32_t IterationCount() const noexcept {
        return iteration_.load(std::memory_order_acquire);
    }

protected:
    bool Initialize() noexcept override {
        return CreateBaseThread(thread_stack_,
                                sizeof(thread_stack_),
                                kThreadPriority,
                                kThreadPriority,
                                0,
                                OS_AUTO_START);
    }

    bool Setup() noexcept override {
        // First-pass force-reconfigure so the underlying CDR / PWM / dither
        // settings are pushed once the manager is ready.
        system_.ForceReconfigureAll();
        return true;
    }

    uint32_t Step() noexcept override {
        const bool ready = manager_.IsInitialized();
        if (ready) mgr_was_ready_.store(true, std::memory_order_release);

        if (ready) {
            system_.ForEachValve([this](Valve& v) {
                if (v.ConsumeDirty()) {
                    (void)v.Apply(manager_);
                }
            });
        }
        iteration_.fetch_add(1, std::memory_order_acq_rel);
        return sample_period_ms_;
    }

    bool Cleanup() noexcept override { return true; }
    bool ResetVariables() noexcept override {
        iteration_.store(0, std::memory_order_release);
        return true;
    }

private:
    WhValveSystem& system_;
    ValveManager&  manager_;
    uint32_t       sample_period_ms_;

    std::atomic<bool>     mgr_was_ready_{false};
    std::atomic<uint32_t> iteration_{0};

    static constexpr std::size_t kThreadStackBytes = 6144;
    static constexpr OS_Uint     kThreadPriority   = 7;
    uint8_t thread_stack_[kThreadStackBytes]{};
};

}  // namespace whvalve
