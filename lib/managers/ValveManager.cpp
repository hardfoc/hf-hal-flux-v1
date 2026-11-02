/**
 * @file ValveManager.cpp
 * @brief Implementation of the unified Flux V1 valve manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "ValveManager.h"
#include "CommChannelsManager.h"
#include "GpioManager.h"
#include "handlers/logger/Logger.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"

static constexpr const char* TAG = "FluxValve";

using flux::hal::ManagerError;

//==============================================================================
// SINGLETON
//==============================================================================

ValveManager& ValveManager::GetInstance() noexcept {
    static ValveManager instance;
    return instance;
}

ValveManager::ValveManager() noexcept {
    channel_states_.fill(ValveChannelState{});
}

//==============================================================================
// INITIALIZATION
//==============================================================================

bool ValveManager::EnsureInitialized() noexcept {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }
    MutexLockGuard lock(mutex_);
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }
    auto result = Initialize();
    if (HfValveErrOk(result)) {
        manager_error_.store(ManagerError::None, std::memory_order_release);
        initialized_.store(true, std::memory_order_release);
    } else {
        (void)Record(result);
    }
    return HfValveErrOk(result);
}

bool ValveManager::IsBankReady(ValveDriverType bank) const noexcept {
    switch (bank) {
        case ValveDriverType::MAX22200: return max22200_valid_;
        case ValveDriverType::TLE92466: return tle92466_valid_;
        default:                        return false;
    }
}

hf_valve_err_t ValveManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux valve manager");

    // A previous failed attempt may have left one bank up; start from nothing.
    ReleaseDriversLocked();

    // Ensure comms and GPIO are ready
    auto& comms = CommChannelsManager::GetInstance();
    if (!comms.EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "CommChannels not initialized — cannot init valves");
        manager_error_.store(ManagerError::DependencyNotReady, std::memory_order_release);
        return Record(hf_valve_err_t::VALVE_ERR_DEPENDENCY_NOT_READY);
    }

    auto& gpio = GpioManager::GetInstance();
    if (!gpio.EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "GPIO manager not initialized — cannot init valves");
        manager_error_.store(ManagerError::DependencyNotReady, std::memory_order_release);
        return Record(hf_valve_err_t::VALVE_ERR_DEPENDENCY_NOT_READY);
    }

    // ── Initialize each driver independently ─────────────────────────────
    // Both share the valve SPI bus but use separate CS lines and GPIO controls.
    // Both are attempted even if the first fails, so the log and
    // IsBankReady() name every missing part, not just the first.

    Logger::GetInstance().Info(TAG, "--- [1/2] Attempting TLE92466ED proportional driver ---");
    const ManagerError tle_err = InitializeTle92466();
    Logger::GetInstance().Info(TAG, "--- [1/2] TLE92466ED result: %s ---", flux::hal::ToString(tle_err));

    Logger::GetInstance().Info(TAG, "--- [2/2] Attempting MAX22200 solenoid driver ---");
    const ManagerError max_err = InitializeMax22200();
    Logger::GetInstance().Info(TAG, "--- [2/2] MAX22200 result: %s ---", flux::hal::ToString(max_err));

    // ── Fail closed: every fitted bank must be up ────────────────────────
    if (tle_err == ManagerError::None && max_err == ManagerError::None) {
        Logger::GetInstance().Info(TAG, "Both valve drivers operational");
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    // A missing bus or pin is a bind failure; otherwise a chip did not answer.
    const ManagerError err = (tle_err == ManagerError::BindFailed || max_err == ManagerError::BindFailed)
                                 ? ManagerError::BindFailed
                                 : ManagerError::HardwareFault;
    manager_error_.store(err, std::memory_order_release);
    Logger::GetInstance().Error(TAG,
        "Valve manager NOT initialised (fail-closed): TLE92466ED=%s, MAX22200=%s. Every channel operation is "
        "refused; a bank that came up is reachable through its handler accessor for diagnosis only.",
        flux::hal::ToString(tle_err), flux::hal::ToString(max_err));
    return (err == ManagerError::BindFailed) ? hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE
                                             : hf_valve_err_t::VALVE_ERR_HARDWARE_FAULT;
}

ManagerError ValveManager::InitializeMax22200() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing MAX22200 solenoid driver");

    auto& comms = CommChannelsManager::GetInstance();
    auto& gpio  = GpioManager::GetInstance();

    // Get SPI device for MAX22200
    BaseSpi* spi = comms.GetSpiDevice(SpiDeviceId::MAX22200_SOLENOID_DRIVER);
    if (!spi) {
        Logger::GetInstance().Error(TAG, "MAX22200 SPI device not available");
        return ManagerError::BindFailed;
    }

    // Get control GPIOs
    BaseGpio* enable_gpio = gpio.Get(HfFunctionalGpioPin::MAX22200_ENABLE);
    BaseGpio* cmd_gpio    = gpio.Get(HfFunctionalGpioPin::MAX22200_CMD);
    BaseGpio* fault_gpio  = gpio.Get(HfFunctionalGpioPin::MAX22200_FAULT_N);

    if (!enable_gpio || !cmd_gpio) {
        Logger::GetInstance().Error(TAG, "MAX22200 control GPIOs not available (EN=%p, CMD=%p)",
                                   static_cast<void*>(enable_gpio), static_cast<void*>(cmd_gpio));
        return ManagerError::BindFailed;
    }

    // Set fault pin as input
    if (fault_gpio) {
        fault_gpio->SetDirection(hf_gpio_direction_t::HF_GPIO_DIRECTION_INPUT);
    }

    // Construct the handler in place (member storage, no heap)
    max22200_.emplace(*spi, *enable_gpio, *cmd_gpio, fault_gpio);

    // Configure board settings
    max22200::BoardConfig board_cfg(15.0f, false); // 15kΩ RREF, HFS=0 → IFS=1000mA

    if (max22200_->Initialize(board_cfg) != max22200::DriverStatus::OK) {
        Logger::GetInstance().Error(TAG, "MAX22200 driver initialization failed");
        max22200_.reset();
        return ManagerError::HardwareFault;
    }

    max22200_valid_ = true;
    Logger::GetInstance().Info(TAG, "MAX22200 initialized successfully (8 solenoid channels)");
    return ManagerError::None;
}

ManagerError ValveManager::InitializeTle92466() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing TLE92466ED proportional driver");

    auto& comms = CommChannelsManager::GetInstance();
    auto& gpio  = GpioManager::GetInstance();

    // Get SPI device for TLE92466ED
    BaseSpi* spi = comms.GetSpiDevice(SpiDeviceId::TLE92466_LOWSIDE_DRIVER);
    if (!spi) {
        Logger::GetInstance().Error(TAG, "TLE92466ED SPI device not available");
        return ManagerError::BindFailed;
    }

    // Get control GPIOs
    BaseGpio* resn_gpio   = gpio.Get(HfFunctionalGpioPin::TLE92466_RESN);
    BaseGpio* en_gpio     = gpio.Get(HfFunctionalGpioPin::TLE92466_EN);
    BaseGpio* fault_gpio  = gpio.Get(HfFunctionalGpioPin::TLE92466_FAULT_N);

    if (!resn_gpio || !en_gpio) {
        Logger::GetInstance().Error(TAG, "TLE92466ED control GPIOs not available (RESN=%p, EN=%p)",
                                   static_cast<void*>(resn_gpio), static_cast<void*>(en_gpio));
        return ManagerError::BindFailed;
    }

    // Set fault pin as input
    if (fault_gpio) {
        fault_gpio->SetDirection(hf_gpio_direction_t::HF_GPIO_DIRECTION_INPUT);
    }

    // Construct the handler in place (member storage, no heap)
    tle92466_.emplace(*spi, *resn_gpio, *en_gpio, fault_gpio);

    if (!tle92466_->Initialize()) {
        Logger::GetInstance().Error(TAG, "TLE92466ED driver initialization failed");
        tle92466_.reset();
        return ManagerError::HardwareFault;
    }
    // Enter mission mode for normal operation
    if (!tle92466_->EnterMissionMode()) {
        Logger::GetInstance().Warn(TAG, "TLE92466ED failed to enter mission mode");
    }

    // Drive the chip's hardware EN pin HIGH so the output stage can deliver
    // current. Without this every SetChannelCurrent / EnableChannel call
    // succeeds at the SPI level but the chip silently delivers zero current.
    if (!tle92466_->EnableOutputStage()) {
        Logger::GetInstance().Warn(TAG,
            "TLE92466ED EnableOutputStage failed — outputs will stay gated off");
    } else {
        Logger::GetInstance().Info(TAG, "TLE92466ED EN pin HIGH — output stage powered");
    }

    // Clear the FB_FRZ register — the chip's POR default freezes every
    // channel's feedback, so FB_DC / FB_I_AVG / FB_VBAT come back as 0
    // even when the chip is actively driving a coil. Without this the
    // ProportionalValve diagnostic snapshot is permanently stuck at zero
    // current / zero duty regardless of the actual setpoint, and any
    // downstream tracking / closed-loop logic sees garbage. See
    // `hf-tle92466ed-driver/examples/esp32/main/lm_pro_cycle_test.cpp` for
    // the original bench discovery.
    if (!tle92466_->EnableFeedbackUpdates()) {
        Logger::GetInstance().Warn(TAG,
            "TLE92466ED FB_FRZ clear failed — feedback registers will read 0");
    } else {
        Logger::GetInstance().Info(TAG,
            "TLE92466ED FB_FRZ cleared — per-channel feedback updates enabled");
    }

    // Configure VBAT supply-voltage trip thresholds for the 24 V valve rail.
    // The chip's POR defaults (UV ≈ 4 V, OV ≈ 41 V) span almost the full
    // ADC range and will never fire a useful diagnostic on a 24 V system.
    // Formula: register = voltage / 0.16208 V  (per TLE92466ED datasheet).
    //
    // 24 V nominal, ±20 % operating band → flag outside [19 V … 29 V].
    //   UV 19 V → register ≈ 117  (19.0 / 0.16208 = 117.2)
    //   OV 29 V → register ≈ 179  (29.0 / 0.16208 = 178.9)
    //
    // Tighten these once final PSU regulation specs are confirmed; they must
    // remain within the supply's rated output range to avoid false trips from
    // normal load-step droop (inrush during simultaneous valve actuation can
    // sag the rail by several volts on a lightly buffered bench supply).
    constexpr float kVbatUvV = 19.0F;  ///< UV trip: 24 V − 21 % (flag true supply loss)
    constexpr float kVbatOvV = 29.0F;  ///< OV trip: 24 V + 21 % (flag regulator runaway)
    {
        auto* drv = tle92466_->GetDriver();
        auto result = drv ? drv->SetVbatThresholds(kVbatUvV, kVbatOvV)
                          : tle92466ed::DriverResult<void>{tle::unexpected(tle92466ed::DriverError::NotInitialized)};
        if (!result) {
            Logger::GetInstance().Warn(TAG,
                "TLE92466ED SetVbatThresholds(UV=%.1f V, OV=%.1f V) failed — "
                "supply monitoring uses chip defaults (UV≈4 V / OV≈41 V)",
                static_cast<double>(kVbatUvV), static_cast<double>(kVbatOvV));
        } else {
            Logger::GetInstance().Info(TAG,
                "TLE92466ED VBAT thresholds set: UV=%.1f V, OV=%.1f V",
                static_cast<double>(kVbatUvV), static_cast<double>(kVbatOvV));
        }
    }

    tle92466_valid_ = true;
    Logger::GetInstance().Info(TAG, "TLE92466ED initialized successfully (6 proportional channels)");
    return ManagerError::None;
}

//==============================================================================
// CHANNEL RESOLUTION
//==============================================================================

bool ValveManager::ResolveChannel(ValveChannel channel,
                                  ValveDriverType& driver_type,
                                  uint8_t& hw_channel) noexcept {
    uint8_t idx = static_cast<uint8_t>(channel);
    if (idx >= kValveChannelCount) return false;
    driver_type = kValveChannelMap[idx].driver_type;
    hw_channel  = kValveChannelMap[idx].hw_channel;
    return true;
}

//==============================================================================
// CHANNEL ENABLE / DISABLE
//==============================================================================

hf_valve_err_t ValveManager::EnableChannel(ValveChannel channel) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch))
        return Record(hf_valve_err_t::VALVE_ERR_INVALID_CHANNEL);

    MutexLockGuard lock(mutex_);
    hf_valve_err_t result;

    if (type == ValveDriverType::MAX22200 && max22200_valid_) {
        result = MaxDriverStatusToValveError(max22200_->EnableChannel(hw_ch));
    } else if (type == ValveDriverType::TLE92466 && tle92466_valid_) {
        auto dr = tle92466_->EnableChannel(hw_ch);
        result = dr.has_value() ? hf_valve_err_t::VALVE_SUCCESS
                                : TleDriverErrorToValveError(dr.error());
    } else {
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);
    }

    if (HfValveErrOk(result)) {
        auto& state = channel_states_[static_cast<uint8_t>(channel)];
        state.enabled = true;
        state.activation_count++;
    } else {
        channel_states_[static_cast<uint8_t>(channel)].error_count++;
        Logger::GetInstance().Error(TAG, "EnableChannel(%.*s) failed: %s",
            static_cast<int>(kValveChannelMap[static_cast<uint8_t>(channel)].name.size()),
            kValveChannelMap[static_cast<uint8_t>(channel)].name.data(),
            HfValveErrToString(result));
    }
    return Record(result);
}

hf_valve_err_t ValveManager::DisableChannel(ValveChannel channel) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch))
        return Record(hf_valve_err_t::VALVE_ERR_INVALID_CHANNEL);

    MutexLockGuard lock(mutex_);
    hf_valve_err_t result;

    if (type == ValveDriverType::MAX22200 && max22200_valid_) {
        result = MaxDriverStatusToValveError(max22200_->DisableChannel(hw_ch));
    } else if (type == ValveDriverType::TLE92466 && tle92466_valid_) {
        auto dr = tle92466_->DisableChannel(hw_ch);
        result = dr.has_value() ? hf_valve_err_t::VALVE_SUCCESS
                                : TleDriverErrorToValveError(dr.error());
    } else {
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);
    }

    if (HfValveErrOk(result)) {
        channel_states_[static_cast<uint8_t>(channel)].enabled = false;
    } else {
        channel_states_[static_cast<uint8_t>(channel)].error_count++;
        Logger::GetInstance().Error(TAG, "DisableChannel(%.*s) failed: %s",
            static_cast<int>(kValveChannelMap[static_cast<uint8_t>(channel)].name.size()),
            kValveChannelMap[static_cast<uint8_t>(channel)].name.data(),
            HfValveErrToString(result));
    }
    return Record(result);
}

hf_valve_err_t ValveManager::EnableChannelsMask(uint16_t mask) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    MutexLockGuard lock(mutex_);

    hf_valve_err_t first_error = hf_valve_err_t::VALVE_SUCCESS;

    // Lower 8 bits → MAX22200
    if (max22200_valid_) {
        uint8_t sol_mask = static_cast<uint8_t>(mask & 0xFF);
        if (sol_mask != 0) {
            auto status = max22200_->SetChannelsMask(sol_mask);
            auto err = MaxDriverStatusToValveError(status);
            if (!HfValveErrOk(err)) {
                if (HfValveErrOk(first_error)) first_error = err;
            } else {
                for (uint8_t i = 0; i < 8; ++i) {
                    if (sol_mask & (1u << i)) {
                        channel_states_[i].enabled = true;
                        channel_states_[i].activation_count++;
                    }
                }
            }
        }
    }

    // Upper 6 bits → TLE92466ED (bits 8-13)
    if (tle92466_valid_) {
        for (uint8_t i = 0; i < 6; ++i) {
            if (mask & (1u << (8 + i))) {
                auto dr = tle92466_->EnableChannel(i);
                if (!dr.has_value()) {
                    auto err = TleDriverErrorToValveError(dr.error());
                    if (HfValveErrOk(first_error)) first_error = err;
                } else {
                    channel_states_[8 + i].enabled = true;
                    channel_states_[8 + i].activation_count++;
                }
            }
        }
    }

    return Record(first_error);
}

hf_valve_err_t ValveManager::DisableAllChannels() noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    MutexLockGuard lock(mutex_);
    return Record(DisableAllLocked());
}

hf_valve_err_t ValveManager::DisableAllLocked() noexcept {
    hf_valve_err_t first_error = hf_valve_err_t::VALVE_SUCCESS;

    if (max22200_valid_) {
        auto err = MaxDriverStatusToValveError(max22200_->DisableAllChannels());
        if (!HfValveErrOk(err) && HfValveErrOk(first_error)) first_error = err;
    }
    if (tle92466_valid_) {
        auto dr = tle92466_->DisableAllChannels();
        if (!dr.has_value()) {
            auto err = TleDriverErrorToValveError(dr.error());
            if (HfValveErrOk(first_error)) first_error = err;
        }
    }

    for (auto& state : channel_states_) {
        state.enabled = false;
    }
    return Record(first_error);
}

bool ValveManager::IsChannelEnabled(ValveChannel channel) const noexcept {
    uint8_t idx = static_cast<uint8_t>(channel);
    if (idx >= static_cast<uint8_t>(ValveChannel::CHANNEL_COUNT)) return false;
    return channel_states_[idx].enabled;
}

//==============================================================================
// SOLENOID CONFIGURATION (MAX22200)
//==============================================================================

hf_valve_err_t ValveManager::ConfigureSolenoidCdr(ValveChannel channel,
                                        uint16_t hit_ma, uint16_t hold_ma,
                                        float hit_time_ms) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    if (!max22200_valid_)
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::MAX22200)
        return Record(hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);

    MutexLockGuard lock(mutex_);
    auto result = MaxDriverStatusToValveError(
        max22200_->SetupCdrChannel(hw_ch, hit_ma, hold_ma, hit_time_ms));
    if (HfValveErrOk(result)) {
        channel_states_[static_cast<uint8_t>(channel)].configured = true;
    }
    return Record(result);
}

hf_valve_err_t ValveManager::ConfigureSolenoidVdr(ValveChannel channel,
                                        float hit_duty_pct, float hold_duty_pct,
                                        float hit_time_ms) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    if (!max22200_valid_)
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::MAX22200)
        return Record(hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);

    MutexLockGuard lock(mutex_);
    auto result = MaxDriverStatusToValveError(
        max22200_->SetupVdrChannel(hw_ch, hit_duty_pct, hold_duty_pct, hit_time_ms));
    if (HfValveErrOk(result)) {
        channel_states_[static_cast<uint8_t>(channel)].configured = true;
        channel_states_[static_cast<uint8_t>(channel)].duty_percent = hold_duty_pct;
    }
    return Record(result);
}

//==============================================================================
// PROPORTIONAL CONFIGURATION (TLE92466ED)
//==============================================================================

hf_valve_err_t ValveManager::SetProportionalCurrent(ValveChannel channel, uint16_t current_ma) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    if (!tle92466_valid_)
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::TLE92466)
        return Record(hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);

    MutexLockGuard lock(mutex_);
    auto dr = tle92466_->SetChannelCurrent(hw_ch, current_ma);
    if (dr.has_value()) {
        channel_states_[static_cast<uint8_t>(channel)].current_ma = current_ma;
        channel_states_[static_cast<uint8_t>(channel)].configured = true;
        return hf_valve_err_t::VALVE_SUCCESS;
    }
    return Record(TleDriverErrorToValveError(dr.error()));
}

hf_valve_err_t ValveManager::ConfigureProportionalPwm(ValveChannel channel,
                                            float frequency_hz) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    if (!tle92466_valid_)
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::TLE92466)
        return Record(hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);

    if (frequency_hz <= 0.0f) {
        Logger::GetInstance().Error(TAG, "PWM frequency must be > 0 (got %.1f Hz)",
                                   static_cast<double>(frequency_hz));
        return Record(hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER);
    }

    // Convert frequency (Hz) → period (µs) and let the driver handle
    // the mantissa/exponent register encoding internally.
    const float period_us = 1'000'000.0f / frequency_hz;

    auto* driver = tle92466_->GetDriver();
    if (!driver) return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);

    MutexLockGuard lock(mutex_);
    auto result = driver->ConfigurePwmPeriod(
        static_cast<tle92466ed::Channel>(hw_ch), period_us);
    if (result.has_value()) {
        channel_states_[static_cast<uint8_t>(channel)].pwm_frequency_hz = frequency_hz;
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    Logger::GetInstance().Error(TAG,
        "ConfigurePwmPeriod failed for channel %u (freq=%.1f Hz, period=%.1f µs)",
        hw_ch, static_cast<double>(frequency_hz), static_cast<double>(period_us));
    channel_states_[static_cast<uint8_t>(channel)].error_count++;
    return Record(TleDriverErrorToValveError(result.error()));
}

hf_valve_err_t ValveManager::ConfigureProportionalDither(ValveChannel channel,
                                               float amplitude_ma,
                                               float frequency_hz) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    if (!tle92466_valid_)
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::TLE92466)
        return Record(hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);

    if (amplitude_ma < 0.0f || frequency_hz <= 0.0f) {
        Logger::GetInstance().Error(TAG, "Invalid dither params: amplitude=%.1f mA, freq=%.1f Hz",
                                   static_cast<double>(amplitude_ma),
                                   static_cast<double>(frequency_hz));
        return Record(hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER);
    }

    // Use the driver's high-level ConfigureDither which handles the
    // step_size / num_steps / flat_steps computation internally.
    auto* driver = tle92466_->GetDriver();
    if (!driver) return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);

    MutexLockGuard lock(mutex_);
    auto result = driver->ConfigureDither(
        static_cast<tle92466ed::Channel>(hw_ch), amplitude_ma, frequency_hz);
    if (result.has_value()) {
        auto& state = channel_states_[static_cast<uint8_t>(channel)];
        state.dither_amplitude_ma = amplitude_ma;
        state.dither_frequency_hz = frequency_hz;
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    Logger::GetInstance().Error(TAG, "ConfigureDither failed for channel %u", hw_ch);
    channel_states_[static_cast<uint8_t>(channel)].error_count++;
    return Record(TleDriverErrorToValveError(result.error()));
}

//==============================================================================
// FAULT MANAGEMENT
//==============================================================================

bool ValveManager::HasFault() noexcept {
    return HasSolenoidFault() || HasProportionalFault();
}

hf_valve_err_t ValveManager::ClearAllFaults() noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return Record(hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    MutexLockGuard lock(mutex_);

    hf_valve_err_t first_error = hf_valve_err_t::VALVE_SUCCESS;

    if (max22200_valid_) {
        auto err = MaxDriverStatusToValveError(max22200_->ClearFaults());
        if (!HfValveErrOk(err) && HfValveErrOk(first_error)) first_error = err;
    }
    if (tle92466_valid_) {
        auto dr = tle92466_->ClearFaults();
        if (!dr.has_value()) {
            auto err = TleDriverErrorToValveError(dr.error());
            if (HfValveErrOk(first_error)) first_error = err;
        }
    }
    return Record(first_error);
}

bool ValveManager::HasSolenoidFault() noexcept {
    if (!max22200_valid_) return false;
    return max22200_->HasFault();
}

bool ValveManager::HasProportionalFault() noexcept {
    if (!tle92466_valid_) return false;
    return tle92466_->HasFault();
}

hf_valve_err_t ValveManager::GetSolenoidFaults(max22200::FaultStatus& faults) noexcept {
    if (!max22200_valid_)
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);
    return Record(MaxDriverStatusToValveError(max22200_->ReadFaultRegister(faults)));
}

hf_valve_err_t ValveManager::GetProportionalFaults(tle92466ed::FaultReport& report) noexcept {
    if (!tle92466_valid_)
        return Record(hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);
    auto dr = tle92466_->GetFaultReport(report);
    return dr.has_value() ? hf_valve_err_t::VALVE_SUCCESS
                          : TleDriverErrorToValveError(dr.error());
}

//==============================================================================
// CHANNEL INFORMATION
//==============================================================================

const ValveChannelState* ValveManager::GetChannelState(ValveChannel channel) const noexcept {
    uint8_t idx = static_cast<uint8_t>(channel);
    if (idx >= static_cast<uint8_t>(ValveChannel::CHANNEL_COUNT)) return nullptr;
    return &channel_states_[idx];
}

const ValveChannelInfo* ValveManager::GetChannelInfo(std::string_view name) const noexcept {
    for (const auto& info : kValveChannelMap) {
        if (info.name == name) return &info;
    }
    return nullptr;
}

ValveChannel ValveManager::FindChannel(std::string_view name) const noexcept {
    for (const auto& info : kValveChannelMap) {
        if (info.name == name) return info.channel;
    }
    return ValveChannel::CHANNEL_COUNT;
}

//==============================================================================
// DIRECT DRIVER ACCESS
//==============================================================================

Max22200Handler* ValveManager::GetMax22200Handler() noexcept {
    return max22200_valid_ ? &*max22200_ : nullptr;
}

Tle92466edHandler* ValveManager::GetTle92466Handler() noexcept {
    return tle92466_valid_ ? &*tle92466_ : nullptr;
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

hf_valve_err_t ValveManager::GetSystemDiagnostics(ValveSystemDiagnostics& diag) const noexcept {
    diag.max22200_initialized  = max22200_valid_;
    diag.tle92466_initialized  = tle92466_valid_;
    diag.total_channels        = static_cast<uint8_t>(ValveChannel::CHANNEL_COUNT);
    diag.configured_channels   = 0;
    diag.active_channels       = 0;
    diag.total_activations     = 0;
    diag.total_errors          = 0;

    for (const auto& state : channel_states_) {
        if (state.configured) ++diag.configured_channels;
        if (state.enabled)    ++diag.active_channels;
        diag.total_activations += state.activation_count;
        diag.total_errors      += state.error_count;
    }

    // Fail-closed: healthy only when the manager is up (both banks) and nothing has failed.
    diag.system_healthy = initialized_.load(std::memory_order_acquire) && max22200_valid_ && tle92466_valid_ &&
                          (diag.total_errors == 0);
    return hf_valve_err_t::VALVE_SUCCESS;
}

void ValveManager::DumpStatistics() const noexcept {
    auto& log = Logger::GetInstance();
    log.Info(TAG, "=== Flux Valve Manager Statistics ===");
    log.Info(TAG, "  MAX22200: %s", max22200_valid_ ? "OK" : "NOT AVAILABLE");
    log.Info(TAG, "  TLE92466ED: %s", tle92466_valid_ ? "OK" : "NOT AVAILABLE");

    for (size_t i = 0; i < kValveChannelCount; ++i) {
        const auto& info  = kValveChannelMap[i];
        const auto& state = channel_states_[i];
        if (info.driver_type == ValveDriverType::MAX22200) {
            log.Info(TAG, "  [%.*s] SOL | cfg=%s en=%s | acts=%u errs=%u | duty=%.1f%%",
                     static_cast<int>(info.name.size()), info.name.data(),
                     state.configured ? "Y" : "N",
                     state.enabled    ? "Y" : "N",
                     state.activation_count,
                     state.error_count,
                     static_cast<double>(state.duty_percent));
        } else {
            log.Info(TAG, "  [%.*s] PRP | cfg=%s en=%s | acts=%u errs=%u | I=%umA pwm=%.0fHz dith=%.0fmA@%.0fHz",
                     static_cast<int>(info.name.size()), info.name.data(),
                     state.configured ? "Y" : "N",
                     state.enabled    ? "Y" : "N",
                     state.activation_count,
                     state.error_count,
                     state.current_ma,
                     static_cast<double>(state.pwm_frequency_hz),
                     static_cast<double>(state.dither_amplitude_ma),
                     static_cast<double>(state.dither_frequency_hz));
        }
    }

    // The handlers' dump calls read registers and are not const; the dump does not change state.
    if (max22200_valid_) const_cast<Max22200Handler&>(*max22200_).DumpDiagnostics();
    if (tle92466_valid_) const_cast<Tle92466edHandler&>(*tle92466_).DumpDiagnostics();

    log.Info(TAG, "=== End Flux Valve Manager Statistics ===");
}

//==============================================================================
// DEINITIALIZE
//==============================================================================

bool ValveManager::Deinitialize() noexcept {
    MutexLockGuard lock(mutex_);

    if (initialized_.load(std::memory_order_acquire) || max22200_valid_ || tle92466_valid_) {
        Logger::GetInstance().Info(TAG, "Deinitializing Flux Valve manager");
        // Outputs off before the handlers go away.
        const hf_valve_err_t off = DisableAllLocked();
        if (!HfValveErrOk(off)) {
            Logger::GetInstance().Warn(TAG, "DisableAll during deinit failed: %s", HfValveErrToString(off));
        }
    }

    // Reset channel states
    for (auto& state : channel_states_) {
        state = ValveChannelState{};
    }

    ReleaseDriversLocked();

    initialized_.store(false, std::memory_order_release);
    manager_error_.store(ManagerError::NotInitialized, std::memory_order_release);
    return true;
}

void ValveManager::ReleaseDriversLocked() noexcept {
    max22200_valid_ = false;
    max22200_.reset();
    tle92466_valid_ = false;
    tle92466_.reset();
}
