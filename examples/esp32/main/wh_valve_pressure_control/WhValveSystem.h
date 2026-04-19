/**
 * @file WhValveSystem.h
 * @brief Compile-time-fixed container for every WH bench valve.
 *
 * @details Owns one `SolenoidValve` instance per `SolenoidId` and one
 *          `ProportionalValve` instance per `ProportionalId` in fixed
 *          storage. Provides O(1) lookup by enum and a small `ForEach*`
 *          interface used by the control / diagnostics threads.
 *
 *          The system does NOT own the underlying Flux::ValveManager; it
 *          accepts a reference each time the threads call into it. That
 *          keeps the system cheap to construct and side-steps the (one-shot)
 *          init failure of the underlying drivers — the threads still run,
 *          they just keep reporting `VALVE_ERR_NOT_INITIALIZED`.
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "ProportionalValve.h"
#include "SolenoidValve.h"
#include "WhValveCatalog.h"

#include <array>
#include <cstddef>

namespace whvalve {

class WhValveSystem {
public:
    WhValveSystem() noexcept
        : solenoids_{ MakeSolenoidArray(std::make_index_sequence<kNumSolenoids>{}) },
          proportionals_{ MakeProportionalArray(std::make_index_sequence<kNumProportionals>{}) }
    {}

    WhValveSystem(const WhValveSystem&) = delete;
    WhValveSystem& operator=(const WhValveSystem&) = delete;

    // ── Lookup ──────────────────────────────────────────────────────────────

    SolenoidValve& Get(SolenoidId id) noexcept {
        return solenoids_[static_cast<std::size_t>(id)];
    }
    const SolenoidValve& Get(SolenoidId id) const noexcept {
        return solenoids_[static_cast<std::size_t>(id)];
    }

    ProportionalValve& Get(ProportionalId id) noexcept {
        return proportionals_[static_cast<std::size_t>(id)];
    }
    const ProportionalValve& Get(ProportionalId id) const noexcept {
        return proportionals_[static_cast<std::size_t>(id)];
    }

    // ── Iteration helpers (used by control / diagnostics threads) ──────────

    template <typename Fn> void ForEachSolenoid(Fn&& fn) noexcept {
        for (auto& v : solenoids_) fn(v);
    }
    template <typename Fn> void ForEachProportional(Fn&& fn) noexcept {
        for (auto& v : proportionals_) fn(v);
    }
    template <typename Fn> void ForEachValve(Fn&& fn) noexcept {
        for (auto& v : solenoids_)     fn(static_cast<Valve&>(v));
        for (auto& v : proportionals_) fn(static_cast<Valve&>(v));
    }

    constexpr std::size_t SolenoidCount()     const noexcept { return kNumSolenoids; }
    constexpr std::size_t ProportionalCount() const noexcept { return kNumProportionals; }

    /// Force the next Apply() on every valve to push full configuration first.
    void ForceReconfigureAll() noexcept {
        for (auto& v : solenoids_)     { v.ForceReconfigure(); v.MarkTargetDirty(); }
        for (auto& v : proportionals_) { v.ForceReconfigure(); v.MarkTargetDirty(); }
    }

    /// Singleton access (single shared instance for the application).
    static WhValveSystem& GetInstance() noexcept {
        static WhValveSystem instance;
        return instance;
    }

private:
    template <std::size_t... Is>
    static constexpr std::array<SolenoidValve, kNumSolenoids>
    MakeSolenoidArray(std::index_sequence<Is...>) noexcept {
        return std::array<SolenoidValve, kNumSolenoids>{
            SolenoidValve(kSolenoidMap[Is])...
        };
    }

    template <std::size_t... Is>
    static constexpr std::array<ProportionalValve, kNumProportionals>
    MakeProportionalArray(std::index_sequence<Is...>) noexcept {
        return std::array<ProportionalValve, kNumProportionals>{
            ProportionalValve(kProportionalMap[Is])...
        };
    }

    std::array<SolenoidValve,     kNumSolenoids>     solenoids_;
    std::array<ProportionalValve, kNumProportionals> proportionals_;
};

}  // namespace whvalve
