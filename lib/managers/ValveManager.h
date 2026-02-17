/**
 * @file ValveManager.h
 * @brief Unified valve management system for the HardFOC Flux V1 platform.
 *
 * @details Manages all valve outputs on the Flux board through two driver ICs:
 *
 *   1. **MAX22200** — 8-channel octal solenoid/motor driver
 *      - On/off solenoid control with CDR (current drive regulation)
 *      - HIT/HOLD current profiles for fast actuation
 *      - Best for: standard on/off solenoid valves
 *
 *   2. **TLE92466ED** — 6-channel low-side driver with current regulation
 *      - Proportional current control per channel
 *      - PWM-based control for proportional valves
 *      - Best for: proportional solenoid valves, PWM-controlled actuators
 *
 * The ValveManager provides a unified, type-safe API to configure and control
 * all 14 valve channels through a single interface. Each channel is identified
 * by a compile-time enum and its driver type is resolved at registration.
 *
 * Design principles (lessons from Vortex):
 *  - All channel descriptors are stack-allocated in fixed-size arrays.
 *  - Handler instances are owned by value where possible.
 *  - No heap allocation for channel lookup — O(1) index-based access.
 *  - String lookups use linear scan over a small constexpr table.
 *  - Thread-safe via RtosMutex on state-changing operations.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#ifndef FLUX_VALVE_MANAGER_H_
#define FLUX_VALVE_MANAGER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/RtosMutex.h"
#include "errors/HfValveError.h"
#include "handlers/max22200/Max22200Handler.h"
#include "handlers/tle92466ed/Tle92466edHandler.h"

//==============================================================================
// VALVE CHANNEL IDENTIFIERS
//==============================================================================

/**
 * @brief Identifies which driver IC backend a valve channel belongs to.
 */
enum class ValveDriverType : uint8_t {
    MAX22200  = 0,  ///< MAX22200 octal solenoid driver
    TLE92466  = 1,  ///< TLE92466ED six-channel proportional driver
    UNKNOWN   = 0xFF
};

/**
 * @brief Unified valve channel identifier — covers all 14 channels.
 *
 * Channels 0‒7  → MAX22200 (octal solenoid driver)
 * Channels 8‒13 → TLE92466ED (six-channel proportional driver)
 */
enum class ValveChannel : uint8_t {
    // MAX22200 channels (0-7)
    SOL_CH0 = 0,   SOL_CH1 = 1,   SOL_CH2 = 2,   SOL_CH3 = 3,
    SOL_CH4 = 4,   SOL_CH5 = 5,   SOL_CH6 = 6,   SOL_CH7 = 7,

    // TLE92466ED channels (8-13)
    PROP_CH0 = 8,  PROP_CH1 = 9,  PROP_CH2 = 10,
    PROP_CH3 = 11, PROP_CH4 = 12, PROP_CH5 = 13,

    CHANNEL_COUNT = 14
};

/**
 * @brief Compile-time channel metadata for the mapping table.
 */
struct ValveChannelInfo {
    ValveChannel channel;           ///< Unified channel enum
    ValveDriverType driver_type;    ///< Which driver IC
    uint8_t hw_channel;             ///< Channel index within the driver (0-based)
    std::string_view name;          ///< Human-readable name
};

/**
 * @brief Compile-time mapping table for all valve channels.
 */
static constexpr ValveChannelInfo kValveChannelMap[] = {
    { ValveChannel::SOL_CH0,  ValveDriverType::MAX22200, 0, "SOL_CH0"  },
    { ValveChannel::SOL_CH1,  ValveDriverType::MAX22200, 1, "SOL_CH1"  },
    { ValveChannel::SOL_CH2,  ValveDriverType::MAX22200, 2, "SOL_CH2"  },
    { ValveChannel::SOL_CH3,  ValveDriverType::MAX22200, 3, "SOL_CH3"  },
    { ValveChannel::SOL_CH4,  ValveDriverType::MAX22200, 4, "SOL_CH4"  },
    { ValveChannel::SOL_CH5,  ValveDriverType::MAX22200, 5, "SOL_CH5"  },
    { ValveChannel::SOL_CH6,  ValveDriverType::MAX22200, 6, "SOL_CH6"  },
    { ValveChannel::SOL_CH7,  ValveDriverType::MAX22200, 7, "SOL_CH7"  },
    { ValveChannel::PROP_CH0, ValveDriverType::TLE92466, 0, "PROP_CH0" },
    { ValveChannel::PROP_CH1, ValveDriverType::TLE92466, 1, "PROP_CH1" },
    { ValveChannel::PROP_CH2, ValveDriverType::TLE92466, 2, "PROP_CH2" },
    { ValveChannel::PROP_CH3, ValveDriverType::TLE92466, 3, "PROP_CH3" },
    { ValveChannel::PROP_CH4, ValveDriverType::TLE92466, 4, "PROP_CH4" },
    { ValveChannel::PROP_CH5, ValveDriverType::TLE92466, 5, "PROP_CH5" },
};

static constexpr size_t kValveChannelCount = sizeof(kValveChannelMap) / sizeof(kValveChannelMap[0]);

//==============================================================================
// VALVE STATUS STRUCTURES
//==============================================================================

/**
 * @brief Per-channel runtime state tracked by the manager.
 */
struct ValveChannelState {
    bool enabled{false};            ///< Whether channel output is currently active
    bool configured{false};         ///< Whether channel has been configured
    uint16_t current_ma{0};         ///< Last-set current setpoint (for proportional)
    float duty_percent{0.0f};       ///< Last-set duty cycle (for VDR solenoids)
    float pwm_frequency_hz{0.0f};   ///< Last-set PWM frequency (for proportional)
    float dither_amplitude_ma{0.0f};///< Last-set dither amplitude (for proportional)
    float dither_frequency_hz{0.0f};///< Last-set dither frequency (for proportional)
    uint32_t activation_count{0};   ///< Total number of enable events
    uint32_t error_count{0};        ///< Total errors on this channel
};

/**
 * @brief Diagnostics snapshot for the entire valve subsystem.
 */
struct ValveSystemDiagnostics {
    bool system_healthy;
    bool max22200_initialized;
    bool tle92466_initialized;
    uint8_t total_channels;
    uint8_t configured_channels;
    uint8_t active_channels;
    uint32_t total_activations;
    uint32_t total_errors;
};

//==============================================================================
// VALVE MANAGER
//==============================================================================

/**
 * @class ValveManager
 * @brief Unified singleton for controlling all valve channels on the Flux board.
 *
 * Provides a single API surface for both solenoid (MAX22200) and proportional
 * (TLE92466ED) valve channels. The manager owns both handler instances and
 * resolves channel requests to the correct backend automatically.
 */
class ValveManager {
public:
    //==========================================================================
    // SINGLETON
    //==========================================================================

    static ValveManager& GetInstance() noexcept;

    ValveManager(const ValveManager&) = delete;
    ValveManager& operator=(const ValveManager&) = delete;
    ValveManager(ValveManager&&) = delete;
    ValveManager& operator=(ValveManager&&) = delete;

    //==========================================================================
    // INITIALIZATION
    //==========================================================================

    [[nodiscard]] hf_valve_err_t EnsureInitialized() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

    //==========================================================================
    // CHANNEL ENABLE / DISABLE (unified for both drivers)
    //==========================================================================

    /** @brief Enable a valve channel output. */
    [[nodiscard]] hf_valve_err_t EnableChannel(ValveChannel channel) noexcept;

    /** @brief Disable a valve channel output. */
    [[nodiscard]] hf_valve_err_t DisableChannel(ValveChannel channel) noexcept;

    /** @brief Enable multiple channels by bitmask (lower 8 bits → MAX22200, upper 6 → TLE). */
    [[nodiscard]] hf_valve_err_t EnableChannelsMask(uint16_t mask) noexcept;

    /** @brief Disable all channels on both drivers. */
    [[nodiscard]] hf_valve_err_t DisableAllChannels() noexcept;

    /** @brief Check if a channel is currently enabled. */
    [[nodiscard]] bool IsChannelEnabled(ValveChannel channel) const noexcept;

    //==========================================================================
    // SOLENOID CONFIGURATION (MAX22200-specific)
    //==========================================================================

    /**
     * @brief Configure a solenoid channel with CDR (current drive regulation).
     * @param channel  Must be SOL_CH0..SOL_CH7
     * @param hit_ma   HIT (inrush) current in milliamps
     * @param hold_ma  HOLD (sustain) current in milliamps
     * @param hit_time_ms  Duration of HIT phase in milliseconds
     * @return VALVE_SUCCESS on success, specific error code on failure
     */
    [[nodiscard]] hf_valve_err_t ConfigureSolenoidCdr(ValveChannel channel,
                                            uint16_t hit_ma, uint16_t hold_ma,
                                            float hit_time_ms) noexcept;

    /**
     * @brief Configure a solenoid channel with VDR (voltage drive regulation).
     * @param channel  Must be SOL_CH0..SOL_CH7
     * @param hit_duty_pct  HIT duty cycle percentage (0-100)
     * @param hold_duty_pct  HOLD duty cycle percentage (0-100)
     * @param hit_time_ms  Duration of HIT phase in milliseconds
     * @return VALVE_SUCCESS on success, specific error code on failure
     */
    [[nodiscard]] hf_valve_err_t ConfigureSolenoidVdr(ValveChannel channel,
                                            float hit_duty_pct, float hold_duty_pct,
                                            float hit_time_ms) noexcept;

    //==========================================================================
    // PROPORTIONAL CONFIGURATION (TLE92466ED-specific)
    //==========================================================================

    /**
     * @brief Set current setpoint for a proportional valve channel.
     * @param channel  Must be PROP_CH0..PROP_CH5
     * @param current_ma  Target current in milliamps (0–2000 single, 0–4000 parallel)
     * @return VALVE_SUCCESS on success, specific error code on failure
     */
    [[nodiscard]] hf_valve_err_t SetProportionalCurrent(ValveChannel channel, uint16_t current_ma) noexcept;

    /**
     * @brief Configure PWM frequency for a proportional channel.
     *
     * Accepts a frequency in Hz and internally converts to the TLE92466ED
     * register encoding (mantissa, exponent, low-freq-range bit).
     *
     * Valid range: ~30 Hz to 8 MHz.
     *   - Standard range (≥ 245 Hz): T = MANT × 2^EXP × 0.125 µs
     *   - Low-freq range  (< 245 Hz): T = MANT × 2^EXP × 1.0 µs
     *
     * @param channel        Must be PROP_CH0..PROP_CH5
     * @param frequency_hz   Desired PWM frequency in Hz
     * @return VALVE_SUCCESS on success, specific error code on failure
     */
    [[nodiscard]] hf_valve_err_t ConfigureProportionalPwm(ValveChannel channel,
                                                float frequency_hz) noexcept;

    /**
     * @brief Configure dither for a proportional channel.
     *
     * Dither superimposes a small oscillation on the current setpoint to
     * reduce valve stiction and improve proportional control linearity.
     *
     * @param channel         Must be PROP_CH0..PROP_CH5
     * @param amplitude_ma    Peak-to-peak dither amplitude in milliamps
     * @param frequency_hz    Dither oscillation frequency in Hz
     * @return VALVE_SUCCESS on success, specific error code on failure
     */
    [[nodiscard]] hf_valve_err_t ConfigureProportionalDither(ValveChannel channel,
                                                   float amplitude_ma,
                                                   float frequency_hz) noexcept;

    //==========================================================================
    // STATUS & FAULT MANAGEMENT
    //==========================================================================

    /** @brief Check if any driver reports a fault. */
    [[nodiscard]] bool HasFault() noexcept;

    /** @brief Clear faults on both drivers. */
    [[nodiscard]] hf_valve_err_t ClearAllFaults() noexcept;

    /** @brief Check for MAX22200-specific faults. */
    [[nodiscard]] bool HasSolenoidFault() noexcept;

    /** @brief Check for TLE92466ED-specific faults. */
    [[nodiscard]] bool HasProportionalFault() noexcept;

    /** @brief Get MAX22200 fault status. */
    [[nodiscard]] hf_valve_err_t GetSolenoidFaults(max22200::FaultStatus& faults) noexcept;

    /** @brief Get TLE92466ED fault report. */
    [[nodiscard]] hf_valve_err_t GetProportionalFaults(tle92466ed::FaultReport& report) noexcept;

    //==========================================================================
    // CHANNEL INFORMATION
    //==========================================================================

    /** @brief Get channel state by enum. */
    [[nodiscard]] const ValveChannelState* GetChannelState(ValveChannel channel) const noexcept;

    /** @brief Get channel info by string name. */
    [[nodiscard]] const ValveChannelInfo* GetChannelInfo(std::string_view name) const noexcept;

    /** @brief Find a channel enum by name. Returns CHANNEL_COUNT if not found. */
    [[nodiscard]] ValveChannel FindChannel(std::string_view name) const noexcept;

    //==========================================================================
    // DIRECT DRIVER ACCESS (for advanced use)
    //==========================================================================

    /** @brief Get the MAX22200 handler (for advanced solenoid operations). */
    [[nodiscard]] Max22200Handler* GetMax22200Handler() noexcept;

    /** @brief Get the TLE92466ED handler (for advanced proportional operations). */
    [[nodiscard]] Tle92466edHandler* GetTle92466Handler() noexcept;

    //==========================================================================
    // DIAGNOSTICS
    //==========================================================================

    [[nodiscard]] bool GetSystemDiagnostics(ValveSystemDiagnostics& diag) const noexcept;
    void DumpStatistics() const noexcept;

private:
    ValveManager() noexcept;
    ~ValveManager() = default;

    hf_valve_err_t Initialize() noexcept;
    bool InitializeMax22200() noexcept;
    bool InitializeTle92466() noexcept;

    /** @brief Resolve channel enum to hw channel index and driver type. */
    [[nodiscard]] static bool ResolveChannel(ValveChannel channel,
                                             ValveDriverType& driver_type,
                                             uint8_t& hw_channel) noexcept;


    //==========================================================================
    // HANDLER INSTANCES — stack-owned through composition
    //==========================================================================

    // MAX22200 handler state
    Max22200Handler* max22200_{nullptr};
    bool max22200_valid_{false};

    // TLE92466ED handler state
    Tle92466edHandler* tle92466_{nullptr};
    bool tle92466_valid_{false};

    // Owning storage — allocated once, singleton lives forever
    void* max22200_storage_{nullptr};
    void* tle92466_storage_{nullptr};

    //==========================================================================
    // CHANNEL STATE — fixed-size, stack-allocated
    //==========================================================================

    std::array<ValveChannelState, static_cast<size_t>(ValveChannel::CHANNEL_COUNT)> channel_states_{};

    std::atomic<bool> initialized_{false};
    mutable RtosMutex mutex_;
};

//==============================================================================
// CONVENIENCE
//==============================================================================

[[nodiscard]] inline ValveManager& GetValveManager() noexcept {
    return ValveManager::GetInstance();
}

#endif // FLUX_VALVE_MANAGER_H_
