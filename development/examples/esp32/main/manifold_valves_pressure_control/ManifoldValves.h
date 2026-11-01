/**
 * @file ManifoldValves.h
 * @brief Compile-time-fixed container for every WH bench valve.
 *
 * @Browser Owns one `SolenoidValve` instance per `SolenoidId` and one
 *          `ProportionalValve` instance per `ProportionalId` in fixed
 *          storage. Provides O(1) lookup by enum and a small `ForEach*`
 *          interface used by the control / diagnostics threads.
 *
 *          Valve I/O is performed through `FLUX_API.valves` inside
 *          `SolenoidValve` / `ProportionalValve` (`Apply` / `OnDiagnostics`).
 *          The catalog container itself stays a cheap aggregate of valve
 *          objects (no `ValveManager&` passed at each call site).
 *
 *          The system class also offers a one-shot `ConfigureGlobalDpm()`
 *          helper that pushes the MAX22200 plunger-movement detection
 *          parameters used by every solenoid that opted into DPM via its
 *          `Max22200ChannelFeatures::plunger_movement_detection` flag.
 *
 * @copyright HardFOC (internal use only)
 * @date 2026
 */

#pragma once

#include "ProportionalValve.h"
#include "SolenoidValve.h"
#include "ManifoldValvesCatalog.h"
#include "api/Flux.h"

#include <array>
#include <cstddef>

namespace manifold_valves {

class ManifoldValves {
public:
    ManifoldValves() noexcept
        : solenoids_{ MakeSolenoidArray(std::make_index_sequence<kNumSolenoids>{}) },
          proportionals_{ MakeProportionalArray(std::make_index_sequence<kNumProportionals>{}) }
    {}

    ManifoldValves(const ManifoldValves&) = delete;
    ManifoldValves& operator=(const ManifoldValves&) = delete;

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

    /**
     * @brief Push the global MAX22200 Detection-of-Plunger-Movement (DPM)
     *        configuration. Per-channel DPM enable bits are pushed by each
     *        `SolenoidValve::ApplyChannelFeatures()` based on the catalog's
     *        `Max22200ChannelFeatures::plunger_movement_detection` flag.
     *
     * @param start_current_ma  ISTART — current at which the dip search begins.
     * @param dip_threshold_ma  IPTH   — minimum dip amplitude to count as movement.
     * @param debounce_ms       TDEB   — minimum dip duration in milliseconds.
     *
     * @Browser The driver converts the floating-point inputs into the
     *          DPM register encoding using IFS and the active chopping
     *          frequency. Tune the three parameters per the C15/C21
     *          datasheets and the bench's mechanical signature.
     *
     * @note Safe to call multiple times. Returns the underlying driver
     *       error if anything goes wrong.
     */
    hf_valve_err_t ConfigureGlobalDpm(float start_current_ma,
                                      float dip_threshold_ma,
                                      float debounce_ms) noexcept {
        Max22200Handler* h = FLUX_API.valves.GetMax22200Handler();
        if (!h) return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
        Max22200Handler::DriverType* drv = h->GetDriver();
        if (!drv) return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
        return MaxDriverStatusToValveError(
            drv->ConfigureDpm(start_current_ma, dip_threshold_ma, debounce_ms));
    }

    /// Singleton access (single shared instance for the application).
    static ManifoldValves& GetInstance() noexcept {
        static ManifoldValves instance;
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

}  // namespace manifold_valves
