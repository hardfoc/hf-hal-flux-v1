/**
 * @file Valve.h
 * @brief Base class for all WH bench valves — atomic state, lock-free API.
 *
 * @details A `Valve` instance lives for the entire program life and is shared
 *          between three asynchronous parties:
 *
 *            1. Application / control-loop threads — push setpoints
 *               (`RequestPosition`, `RequestState`) and read back the latest
 *               actual state via lock-free atomic loads.
 *
 *            2. The control thread (`WhValveControlThread`) — owns the right
 *               to call into the underlying Flux::ValveManager for this valve
 *               and is the only one that updates the "actual" snapshot.
 *
 *            3. The diagnostics thread (`WhDiagnosticsThread`) — atomically
 *               aggregates fault flags from the driver IC into the per-valve
 *               error / fault counters.
 *
 *          The base class only owns shared state. Each subclass
 *          (`SolenoidValve`, `ProportionalValve`) implements `Apply()` and
 *          `OnDiagnostics()` against the Flux::ValveManager API.
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "WhValveCatalog.h"

#include <atomic>
#include <cstdint>
#include <string_view>

class ValveManager;  // Forward declaration to avoid heavy include.

namespace whvalve {

//==============================================================================
// VALVE STATE / ERROR ENUMS
//==============================================================================

/**
 * @brief Top-level operational state of a valve as observed by the manager.
 *
 * The state machine is deliberately small; richer fault reporting lives in
 * `Valve::GetLastError()` and the per-driver fault structures.
 */
enum class ValveState : uint8_t {
    Uninitialized = 0,  ///< Driver not initialised yet (e.g. SPI brought up but device absent).
    Disabled,           ///< Driver up; output explicitly off.
    Idle,               ///< Hardware ready, no command outstanding (proportional at 0).
    Active,             ///< Last command applied successfully (solenoid energised, proportional > 0).
    Faulted,            ///< Last apply or diagnostic poll reported a fault — see GetLastError().
};

constexpr const char* ValveStateToString(ValveState s) noexcept {
    switch (s) {
        case ValveState::Uninitialized: return "Uninitialized";
        case ValveState::Disabled:      return "Disabled";
        case ValveState::Idle:          return "Idle";
        case ValveState::Active:        return "Active";
        case ValveState::Faulted:       return "Faulted";
    }
    return "?";
}

//==============================================================================
// BASE VALVE CLASS
//==============================================================================

/**
 * @brief Common state owned by every valve — base for SolenoidValve / ProportionalValve.
 *
 * @par Thread safety
 *   - `RequestState` / `RequestPosition` / `Get*` methods are lock-free and
 *     safe to call from any thread at any rate.
 *   - `Apply` / `OnDiagnostics` are intended for the control / diagnostics
 *     threads only; the system class enforces single-writer ownership.
 *
 * @par Atomic snapshot semantics
 *   Three independent atomics are used:
 *     - `target_pos_`  — last setpoint requested by the app (0..1).
 *     - `actual_pos_` — last position the control thread *successfully*
 *                       applied (0..1). 0 ≡ closed, 1 ≡ fully open / energised.
 *     - `state_` and `last_error_` — most recent operational status.
 *   Releases on store / acquires on load give consumers a self-consistent
 *   snapshot (within the same valve) without locks.
 */
class Valve {
public:
    explicit Valve(std::string_view label) noexcept : label_(label) {}
    virtual ~Valve() = default;

    Valve(const Valve&) = delete;
    Valve& operator=(const Valve&) = delete;

    //=========================================================================
    // Identity
    //=========================================================================

    std::string_view Label() const noexcept { return label_; }
    virtual ValveChannel ManagerChannel() const noexcept = 0;
    virtual bool IsMapped() const noexcept { return ManagerChannel() != ValveChannel::CHANNEL_COUNT; }

    //=========================================================================
    // Lock-free setpoint / readback (any thread, any rate)
    //=========================================================================

    /**
     * @brief Request a normalised position 0..1.
     * @details For solenoids this is interpreted as 0 → off, > 0 → on.
     *          For proportionals it linearly maps to current 0..full_flow_ma.
     *          The control thread observes the change on the next iteration
     *          and pushes it to the hardware. Out-of-range values are clamped.
     */
    void RequestPosition(float pos) noexcept {
        if (pos < 0.0f) pos = 0.0f;
        if (pos > 1.0f) pos = 1.0f;
        target_pos_.store(pos, std::memory_order_release);
        target_dirty_.store(true, std::memory_order_release);
    }

    /// Convenience for solenoid-style use; equivalent to RequestPosition(on?1:0).
    void RequestState(bool on) noexcept { RequestPosition(on ? 1.0f : 0.0f); }

    /// Snapshot of the actual position the control thread last managed to set.
    float GetActualPosition() const noexcept {
        return actual_pos_.load(std::memory_order_acquire);
    }

    /// Snapshot of the last setpoint the app pushed (may not yet be applied).
    float GetTargetPosition() const noexcept {
        return target_pos_.load(std::memory_order_acquire);
    }

    bool IsActuallyEnergised() const noexcept { return GetActualPosition() > 0.0f; }

    //=========================================================================
    // Status / diagnostics (lock-free reads)
    //=========================================================================

    ValveState GetState() const noexcept { return state_.load(std::memory_order_acquire); }
    hf_valve_err_t GetLastError() const noexcept { return last_error_.load(std::memory_order_acquire); }

    uint32_t GetApplyCount()      const noexcept { return apply_count_.load(std::memory_order_acquire); }
    uint32_t GetApplyErrorCount() const noexcept { return apply_error_count_.load(std::memory_order_acquire); }
    uint32_t GetFaultCount()      const noexcept { return fault_count_.load(std::memory_order_acquire); }
    uint64_t GetLastFaultTickMs() const noexcept { return last_fault_tick_ms_.load(std::memory_order_acquire); }

    //=========================================================================
    // Manager-internal API (control & diagnostics threads only)
    //=========================================================================

    /**
     * @brief Apply the current setpoint to the underlying driver IC.
     * @param mgr Flux::ValveManager — caller must guarantee mgr.IsInitialized().
     * @return The hf_valve_err_t returned by the underlying call (also stored
     *         in last_error_).
     *
     * @note Implemented by the `SolenoidValve` and `ProportionalValve`
     *       subclasses. Always invoked by `WhValveControlThread`.
     */
    virtual hf_valve_err_t Apply(ValveManager& mgr) noexcept = 0;

    /**
     * @brief Refresh per-valve diagnostics from the driver IC.
     * @param mgr Flux::ValveManager — caller must guarantee mgr.IsInitialized().
     * @return Aggregated error code (success if everything is healthy).
     *
     * @note Invoked by `WhDiagnosticsThread` at a slow polling rate. Each
     *       subclass examines the relevant driver-IC fault structure and
     *       aggregates it into `last_error_` / `fault_count_`.
     */
    virtual hf_valve_err_t OnDiagnostics(ValveManager& mgr) noexcept = 0;

    /**
     * @brief Mark the target as dirty without changing it (used to force the
     *        first apply at startup).
     */
    void MarkTargetDirty() noexcept { target_dirty_.store(true, std::memory_order_release); }

    /**
     * @brief Atomically test-and-clear the dirty bit.
     * @return true if a new setpoint is pending application.
     */
    bool ConsumeDirty() noexcept {
        bool expected = true;
        return target_dirty_.compare_exchange_strong(expected, false,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire);
    }

protected:
    //=========================================================================
    // Hooks for subclasses (single-writer = control thread)
    //=========================================================================

    void SetActualPosition(float pos) noexcept {
        actual_pos_.store(pos, std::memory_order_release);
    }
    void SetState(ValveState s) noexcept {
        state_.store(s, std::memory_order_release);
    }
    void RecordApplyResult(hf_valve_err_t err, bool ok) noexcept {
        last_error_.store(err, std::memory_order_release);
        apply_count_.fetch_add(1, std::memory_order_acq_rel);
        if (!ok) apply_error_count_.fetch_add(1, std::memory_order_acq_rel);
    }
    void RecordFault(hf_valve_err_t err, uint64_t now_ms) noexcept {
        last_error_.store(err, std::memory_order_release);
        if (err != hf_valve_err_t::VALVE_SUCCESS) {
            fault_count_.fetch_add(1, std::memory_order_acq_rel);
            last_fault_tick_ms_.store(now_ms, std::memory_order_release);
            state_.store(ValveState::Faulted, std::memory_order_release);
        }
    }

private:
    std::string_view label_;

    std::atomic<float>          target_pos_{0.0f};
    std::atomic<bool>           target_dirty_{true};   // force first apply
    std::atomic<float>          actual_pos_{0.0f};

    std::atomic<ValveState>     state_{ValveState::Uninitialized};
    std::atomic<hf_valve_err_t> last_error_{hf_valve_err_t::VALVE_SUCCESS};

    std::atomic<uint32_t>       apply_count_{0};
    std::atomic<uint32_t>       apply_error_count_{0};
    std::atomic<uint32_t>       fault_count_{0};
    std::atomic<uint64_t>       last_fault_tick_ms_{0};
};

}  // namespace whvalve
