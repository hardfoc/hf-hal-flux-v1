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
        initialized_.store(true, std::memory_order_release);
    }
    return HfValveErrOk(result);
}

hf_valve_err_t ValveManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux valve manager");

    // Ensure comms and GPIO are ready
    auto& comms = CommChannelsManager::GetInstance();
    if (!comms.EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "CommChannels not initialized — cannot init valves");
        return hf_valve_err_t::VALVE_ERR_DEPENDENCY_NOT_READY;
    }

    auto& gpio = GpioManager::GetInstance();
    if (!gpio.EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "GPIO manager not initialized — cannot init valves");
        return hf_valve_err_t::VALVE_ERR_DEPENDENCY_NOT_READY;
    }

    bool max_ok = InitializeMax22200();
    bool tle_ok = InitializeTle92466();

    Logger::GetInstance().Info(TAG, "Valve manager initialized (MAX22200=%s, TLE92466=%s)",
                              max_ok ? "OK" : "FAIL", tle_ok ? "OK" : "FAIL");

    // At least one driver must succeed
    if (!max_ok && !tle_ok) {
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
    }
    return hf_valve_err_t::VALVE_SUCCESS;
}

bool ValveManager::InitializeMax22200() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing MAX22200 solenoid driver");

    auto& comms = CommChannelsManager::GetInstance();
    auto& gpio  = GpioManager::GetInstance();

    // Get SPI device for MAX22200
    BaseSpi* spi = comms.GetSpiDevice(SpiDeviceId::MAX22200_SOLENOID_DRIVER);
    if (!spi) {
        Logger::GetInstance().Error(TAG, "MAX22200 SPI device not available");
        return false;
    }

    // Get control GPIOs
    BaseGpio* enable_gpio = gpio.Get(HfFunctionalGpioPin::MAX22200_ENABLE);
    BaseGpio* cmd_gpio    = gpio.Get(HfFunctionalGpioPin::MAX22200_CMD);
    BaseGpio* fault_gpio  = gpio.Get(HfFunctionalGpioPin::MAX22200_FAULT_N);

    if (!enable_gpio || !cmd_gpio) {
        Logger::GetInstance().Error(TAG, "MAX22200 control GPIOs not available (EN=%p, CMD=%p)",
                                   enable_gpio, cmd_gpio);
        return false;
    }

    // Set fault pin as input
    if (fault_gpio) {
        fault_gpio->SetDirection(hf_gpio_direction_t::HF_GPIO_DIRECTION_INPUT);
    }

    // Create handler
    auto* handler = new (std::nothrow) Max22200Handler(*spi, *enable_gpio, *cmd_gpio, fault_gpio);
    if (!handler) {
        Logger::GetInstance().Error(TAG, "Failed to allocate MAX22200 handler");
        return false;
    }
    max22200_storage_ = handler;
    max22200_ = handler;

    // Configure board settings
    max22200::BoardConfig board_cfg{};
    board_cfg.external_ref_resistor_ohms = 15000; // 15kΩ RREF
    board_cfg.full_scale_current_ma      = 1000;  // 1A IFS

    if (!handler->Initialize(board_cfg)) {
        Logger::GetInstance().Error(TAG, "MAX22200 driver initialization failed");
        delete handler;
        max22200_storage_ = nullptr;
        max22200_ = nullptr;
        return false;
    }

    max22200_valid_ = true;
    Logger::GetInstance().Info(TAG, "MAX22200 initialized successfully (8 solenoid channels)");
    return true;
}

bool ValveManager::InitializeTle92466() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing TLE92466ED proportional driver");

    auto& comms = CommChannelsManager::GetInstance();
    auto& gpio  = GpioManager::GetInstance();

    // Get SPI device for TLE92466ED
    BaseSpi* spi = comms.GetSpiDevice(SpiDeviceId::TLE92466_LOWSIDE_DRIVER);
    if (!spi) {
        Logger::GetInstance().Error(TAG, "TLE92466ED SPI device not available");
        return false;
    }

    // Get control GPIOs
    BaseGpio* resn_gpio   = gpio.Get(HfFunctionalGpioPin::TLE92466_RESN);
    BaseGpio* en_gpio     = gpio.Get(HfFunctionalGpioPin::TLE92466_EN);
    BaseGpio* fault_gpio  = gpio.Get(HfFunctionalGpioPin::TLE92466_FAULT_N);

    if (!resn_gpio || !en_gpio) {
        Logger::GetInstance().Error(TAG, "TLE92466ED control GPIOs not available (RESN=%p, EN=%p)",
                                   resn_gpio, en_gpio);
        return false;
    }

    // Set fault pin as input
    if (fault_gpio) {
        fault_gpio->SetDirection(hf_gpio_direction_t::HF_GPIO_DIRECTION_INPUT);
    }

    // Create handler
    auto* handler = new (std::nothrow) Tle92466edHandler(*spi, *resn_gpio, *en_gpio, fault_gpio);
    if (!handler) {
        Logger::GetInstance().Error(TAG, "Failed to allocate TLE92466ED handler");
        return false;
    }
    tle92466_storage_ = handler;
    tle92466_ = handler;

    if (!handler->Initialize()) {
        Logger::GetInstance().Error(TAG, "TLE92466ED driver initialization failed");
        delete handler;
        tle92466_storage_ = nullptr;
        tle92466_ = nullptr;
        return false;
    }

    // Enter mission mode for normal operation
    if (!handler->EnterMissionMode()) {
        Logger::GetInstance().Warn(TAG, "TLE92466ED failed to enter mission mode");
    }

    tle92466_valid_ = true;
    Logger::GetInstance().Info(TAG, "TLE92466ED initialized successfully (6 proportional channels)");
    return true;
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
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch))
        return hf_valve_err_t::VALVE_ERR_INVALID_CHANNEL;

    MutexLockGuard lock(mutex_);
    hf_valve_err_t result;

    if (type == ValveDriverType::MAX22200 && max22200_valid_) {
        result = MaxDriverStatusToValveError(max22200_->EnableChannel(hw_ch));
    } else if (type == ValveDriverType::TLE92466 && tle92466_valid_) {
        auto dr = tle92466_->EnableChannel(hw_ch);
        result = dr.has_value() ? hf_valve_err_t::VALVE_SUCCESS
                                : TleDriverErrorToValveError(dr.error());
    } else {
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
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
            HfValveErrName(result));
    }
    return result;
}

hf_valve_err_t ValveManager::DisableChannel(ValveChannel channel) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch))
        return hf_valve_err_t::VALVE_ERR_INVALID_CHANNEL;

    MutexLockGuard lock(mutex_);
    hf_valve_err_t result;

    if (type == ValveDriverType::MAX22200 && max22200_valid_) {
        result = MaxDriverStatusToValveError(max22200_->DisableChannel(hw_ch));
    } else if (type == ValveDriverType::TLE92466 && tle92466_valid_) {
        auto dr = tle92466_->DisableChannel(hw_ch);
        result = dr.has_value() ? hf_valve_err_t::VALVE_SUCCESS
                                : TleDriverErrorToValveError(dr.error());
    } else {
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
    }

    if (HfValveErrOk(result)) {
        channel_states_[static_cast<uint8_t>(channel)].enabled = false;
    } else {
        channel_states_[static_cast<uint8_t>(channel)].error_count++;
        Logger::GetInstance().Error(TAG, "DisableChannel(%.*s) failed: %s",
            static_cast<int>(kValveChannelMap[static_cast<uint8_t>(channel)].name.size()),
            kValveChannelMap[static_cast<uint8_t>(channel)].name.data(),
            HfValveErrName(result));
    }
    return result;
}

hf_valve_err_t ValveManager::EnableChannelsMask(uint16_t mask) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
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

    return first_error;
}

hf_valve_err_t ValveManager::DisableAllChannels() noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    MutexLockGuard lock(mutex_);

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
    return first_error;
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
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    if (!max22200_valid_)
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::MAX22200)
        return hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH;

    MutexLockGuard lock(mutex_);
    auto result = MaxDriverStatusToValveError(
        max22200_->SetupCdrChannel(hw_ch, hit_ma, hold_ma, hit_time_ms));
    if (HfValveErrOk(result)) {
        channel_states_[static_cast<uint8_t>(channel)].configured = true;
    }
    return result;
}

hf_valve_err_t ValveManager::ConfigureSolenoidVdr(ValveChannel channel,
                                        float hit_duty_pct, float hold_duty_pct,
                                        float hit_time_ms) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    if (!max22200_valid_)
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::MAX22200)
        return hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH;

    MutexLockGuard lock(mutex_);
    auto result = MaxDriverStatusToValveError(
        max22200_->SetupVdrChannel(hw_ch, hit_duty_pct, hold_duty_pct, hit_time_ms));
    if (HfValveErrOk(result)) {
        channel_states_[static_cast<uint8_t>(channel)].configured = true;
        channel_states_[static_cast<uint8_t>(channel)].duty_percent = hold_duty_pct;
    }
    return result;
}

//==============================================================================
// PROPORTIONAL CONFIGURATION (TLE92466ED)
//==============================================================================

hf_valve_err_t ValveManager::SetProportionalCurrent(ValveChannel channel, uint16_t current_ma) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    if (!tle92466_valid_)
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::TLE92466)
        return hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH;

    MutexLockGuard lock(mutex_);
    auto dr = tle92466_->SetChannelCurrent(hw_ch, current_ma);
    if (dr.has_value()) {
        channel_states_[static_cast<uint8_t>(channel)].current_ma = current_ma;
        channel_states_[static_cast<uint8_t>(channel)].configured = true;
        return hf_valve_err_t::VALVE_SUCCESS;
    }
    return TleDriverErrorToValveError(dr.error());
}

hf_valve_err_t ValveManager::ConfigureProportionalPwm(ValveChannel channel,
                                            float frequency_hz) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    if (!tle92466_valid_)
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::TLE92466)
        return hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH;

    if (frequency_hz <= 0.0f) {
        Logger::GetInstance().Error(TAG, "PWM frequency must be > 0 (got %.1f Hz)",
                                   static_cast<double>(frequency_hz));
        return hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER;
    }

    // Convert frequency (Hz) → period (µs) and let the driver handle
    // the mantissa/exponent register encoding internally.
    const float period_us = 1'000'000.0f / frequency_hz;

    auto* driver = tle92466_->GetDriver();
    if (!driver) return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

    MutexLockGuard lock(mutex_);
    auto result = driver->ConfigurePwmPeriod(
        static_cast<tle92466ed::Channel>(hw_ch), period_us);
    if (result.ok()) {
        channel_states_[static_cast<uint8_t>(channel)].pwm_frequency_hz = frequency_hz;
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    Logger::GetInstance().Error(TAG,
        "ConfigurePwmPeriod failed for channel %u (freq=%.1f Hz, period=%.1f µs)",
        hw_ch, static_cast<double>(frequency_hz), static_cast<double>(period_us));
    channel_states_[static_cast<uint8_t>(channel)].error_count++;
    return TleDriverErrorToValveError(result.error());
}

hf_valve_err_t ValveManager::ConfigureProportionalDither(ValveChannel channel,
                                               float amplitude_ma,
                                               float frequency_hz) noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
    if (!tle92466_valid_)
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

    ValveDriverType type;
    uint8_t hw_ch;
    if (!ResolveChannel(channel, type, hw_ch) || type != ValveDriverType::TLE92466)
        return hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH;

    if (amplitude_ma < 0.0f || frequency_hz <= 0.0f) {
        Logger::GetInstance().Error(TAG, "Invalid dither params: amplitude=%.1f mA, freq=%.1f Hz",
                                   static_cast<double>(amplitude_ma),
                                   static_cast<double>(frequency_hz));
        return hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER;
    }

    // Use the driver's high-level ConfigureDither which handles the
    // step_size / num_steps / flat_steps computation internally.
    auto* driver = tle92466_->GetDriver();
    if (!driver) return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;

    MutexLockGuard lock(mutex_);
    auto result = driver->ConfigureDither(
        static_cast<tle92466ed::Channel>(hw_ch), amplitude_ma, frequency_hz);
    if (result.ok()) {
        auto& state = channel_states_[static_cast<uint8_t>(channel)];
        state.dither_amplitude_ma = amplitude_ma;
        state.dither_frequency_hz = frequency_hz;
        return hf_valve_err_t::VALVE_SUCCESS;
    }

    Logger::GetInstance().Error(TAG, "ConfigureDither failed for channel %u", hw_ch);
    channel_states_[static_cast<uint8_t>(channel)].error_count++;
    return TleDriverErrorToValveError(result.error());
}

//==============================================================================
// FAULT MANAGEMENT
//==============================================================================

bool ValveManager::HasFault() noexcept {
    return HasSolenoidFault() || HasProportionalFault();
}

hf_valve_err_t ValveManager::ClearAllFaults() noexcept {
    if (!initialized_.load(std::memory_order_acquire))
        return hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED;
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
    return first_error;
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
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
    return MaxDriverStatusToValveError(max22200_->ReadFaultRegister(faults));
}

hf_valve_err_t ValveManager::GetProportionalFaults(tle92466ed::FaultReport& report) noexcept {
    if (!tle92466_valid_)
        return hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE;
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
    return max22200_valid_ ? max22200_ : nullptr;
}

Tle92466edHandler* ValveManager::GetTle92466Handler() noexcept {
    return tle92466_valid_ ? tle92466_ : nullptr;
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

bool ValveManager::GetSystemDiagnostics(ValveSystemDiagnostics& diag) const noexcept {
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

    diag.system_healthy = (max22200_valid_ || tle92466_valid_) && (diag.total_errors == 0);
    return true;
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

    if (max22200_valid_) max22200_->DumpDiagnostics();
    if (tle92466_valid_) tle92466_->DumpDiagnostics();

    log.Info(TAG, "=== End Flux Valve Manager Statistics ===");
}

//==============================================================================
// DEINITIALIZE
//==============================================================================

bool ValveManager::Deinitialize() noexcept {
    MutexLockGuard lock(mutex_);
    if (!initialized_.load(std::memory_order_acquire)) return true;

    Logger::GetInstance().Info(TAG, "Deinitializing Flux Valve manager");

    // Reset channel states
    for (auto& state : channel_states_) {
        state.enabled = false;
        state.configured = false;
        state.activation_count = 0;
        state.error_count = 0;
    }

    // Delete MAX22200 handler
    if (max22200_storage_) {
        delete static_cast<Max22200Handler*>(max22200_storage_);
        max22200_storage_ = nullptr;
        max22200_ = nullptr;
        max22200_valid_ = false;
    }

    // Delete TLE92466ED handler
    if (tle92466_storage_) {
        delete static_cast<Tle92466edHandler*>(tle92466_storage_);
        tle92466_storage_ = nullptr;
        tle92466_ = nullptr;
        tle92466_valid_ = false;
    }

    initialized_.store(false, std::memory_order_release);
    return true;
}
