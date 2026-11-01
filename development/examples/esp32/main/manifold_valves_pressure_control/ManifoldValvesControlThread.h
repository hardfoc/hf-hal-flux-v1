/**
 * @file ManifoldValvesControlThread.h
 * @brief BaseThread that pushes pending valve setpoints to the driver ICs.
 *
 * @details Runs on its own RTOS task at a fixed period (default 10 ms / 100 Hz).
 *          Each iteration walks every valve in the ManifoldValves; for any valve
 *          whose dirty flag is set, it calls `Apply()` so the
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
#include "ManifoldValves.h"
#include "api/Flux.h"

#include <atomic>
#include <cstdint>

namespace manifold_valves {

class ManifoldValvesControlThread final : public BaseThread {
public:
    /**
     * @param system           The shared ManifoldValves instance.
     * @param sample_period_ms Step period in ms. 10 ms = 100 Hz default;
     *                         feel free to drop to 5 ms or raise to 50 ms.
     */
    ManifoldValvesControlThread(ManifoldValves& system,
                         uint32_t       sample_period_ms = 10) noexcept
        : BaseThread("MnfldVlvCtrl"),
          system_(system),
          sample_period_ms_(sample_period_ms) {}

    void SetSamplePeriodMs(uint32_t ms) noexcept {
        if (ms == 0) ms = 1;
        sample_period_ms_ = ms;
    }

    /// True once the thread has at least once observed FLUX valve manager ready.
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
        const bool ready = FLUX_API.valves.IsInitialized();
        if (ready) mgr_was_ready_.store(true, std::memory_order_release);

        if (ready) {
            system_.ForEachValve([this](Valve& v) {
                if (v.ConsumeDirty()) {
                    (void)v.Apply();
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
    ManifoldValves& system_;
    uint32_t       sample_period_ms_;

    std::atomic<bool>     mgr_was_ready_{false};
    std::atomic<uint32_t> iteration_{0};

    // 16 kB stack: the first Apply() on each ProportionalValve enters
    // Configuration Mode, writes a ChannelConfig (held by value on the
    // stack), pushes PWM + dither (each of which allocates its own
    // step/freq/clk-div lookup tables on the stack inside the TLE92466ED
    // driver), then re-enters Mission Mode. With the default 6 kB the
    // task overflows the moment that round-trip starts. 16 kB gives
    // ample headroom for the driver's lambdas, mutex locks and inline
    // ESP_LOG calls without affecting RAM budget meaningfully.
    static constexpr std::size_t kThreadStackBytes = 16384;
    static constexpr OS_Uint     kThreadPriority   = 7;
    uint8_t thread_stack_[kThreadStackBytes]{};
};

}  // namespace manifold_valves
