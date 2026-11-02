/**
 * @file CommChannelsManager.cpp
 * @brief Implementation of the Flux V1 communication channels manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "CommChannelsManager.h"

#include "bsp_bind/FluxPlatformBind.h"
#include "bsp_bind/board/flux_v1/FluxBoardConfig.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"
#include "handlers/logger/Logger.h"

static constexpr const char* TAG = "FluxComm";

namespace {

struct SpiSpeedDisplay {
    uint32_t value;
    const char* unit;
};

SpiSpeedDisplay FormatSpiSpeed(uint32_t hz) noexcept {
    if (hz >= 1'000'000U) {
        return {hz / 1'000'000U, "MHz"};
    }
    return {hz / 1'000U, "kHz"};
}

/** @brief Add one chip select to an initialised bus and log it. nullptr if the CS pin is unmapped. */
BaseSpi* AddSpiDevice(flux::board::SpiBus bus, HfFunctionalGpioPin cs_pin, uint32_t hz, uint8_t mode,
                      const char* label) noexcept {
    const auto* cs_map = GetGpioMapping(cs_pin);
    if (cs_map == nullptr) {
        Logger::GetInstance().Error(TAG, "%s chip-select pin missing from the pin map", label);
        return nullptr;
    }
    const flux::bsp::SpiDeviceSpec spec{static_cast<hf_pin_num_t>(cs_map->physical_pin), hz, mode};
    BaseSpi* dev = flux::bsp::CreateSpiDevice(static_cast<size_t>(bus), spec);
    const SpiSpeedDisplay speed = FormatSpiSpeed(hz);
    if (dev == nullptr) {
        Logger::GetInstance().Error(TAG, "%s SPI device add failed (CS=GPIO%d)", label, cs_map->physical_pin);
        return nullptr;
    }
    Logger::GetInstance().Info(TAG, "%s SPI device added (bus=%u, CS=GPIO%d, speed=%u %s, mode=%u)", label,
                               static_cast<unsigned>(bus), cs_map->physical_pin, speed.value, speed.unit,
                               static_cast<unsigned>(mode));
    return dev;
}

/** @brief Bring up one SPI controller from its pin-map pins. */
bool CreateSpiBusFromPins(flux::board::SpiBus bus, HfFunctionalGpioPin miso, HfFunctionalGpioPin mosi,
                          HfFunctionalGpioPin sck, uint8_t controller, bool use_iomux) noexcept {
    const auto* miso_map = GetGpioMapping(miso);
    const auto* mosi_map = GetGpioMapping(mosi);
    const auto* sck_map = GetGpioMapping(sck);
    if (miso_map == nullptr || mosi_map == nullptr || sck_map == nullptr) {
        return false;
    }
    const flux::bsp::SpiBusSpec spec{controller,
                                     static_cast<hf_pin_num_t>(miso_map->physical_pin),
                                     static_cast<hf_pin_num_t>(mosi_map->physical_pin),
                                     static_cast<hf_pin_num_t>(sck_map->physical_pin),
                                     use_iomux,
                                     flux::board::kSpiTimeoutMs};
    return flux::bsp::CreateSpiBus(static_cast<size_t>(bus), spec);
}

}  // namespace

//==============================================================================
// SINGLETON
//==============================================================================

CommChannelsManager& CommChannelsManager::GetInstance() noexcept {
    static CommChannelsManager instance;
    return instance;
}

//==============================================================================
// INITIALIZATION
//==============================================================================

bool CommChannelsManager::EnsureInitialized() noexcept {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }
    MutexLockGuard lock(mutex_);
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }
    bool ok = Initialize();
    if (ok) {
        manager_error_.store(flux::hal::ManagerError::None, std::memory_order_release);
    } else {
        manager_error_.store(flux::hal::ManagerError::BindFailed, std::memory_order_release);
        UpdateLastError(CommError::INITIALIZATION_FAILED);
    }
    initialized_.store(ok, std::memory_order_release);
    return ok;
}

bool CommChannelsManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux V1 communication channels");
    using flux::board::SpiBus;

    // A previous failed attempt may have left buses up; start from nothing.
    ReleaseLocked();

    //==========================================================================
    // VALVE SPI BUS — shared by MAX22200 and TLE92466ED (pin map SPI2_*)
    //==========================================================================
    spi_bus_valid_ = CreateSpiBusFromPins(SpiBus::Valve, HfFunctionalGpioPin::SPI2_MISO,
                                          HfFunctionalGpioPin::SPI2_MOSI, HfFunctionalGpioPin::SPI2_SCK,
                                          flux::board::kValveSpiController, flux::board::kValveSpiUseIomux);
    if (spi_bus_valid_) {
        spi_devices_[static_cast<uint8_t>(SpiDeviceId::MAX22200_SOLENOID_DRIVER)] =
            AddSpiDevice(SpiBus::Valve, HfFunctionalGpioPin::SPI2_CS_MAX22200, flux::board::kMax22200SpiHz,
                         flux::board::kMax22200SpiMode, "MAX22200");
        spi_devices_[static_cast<uint8_t>(SpiDeviceId::TLE92466_LOWSIDE_DRIVER)] =
            AddSpiDevice(SpiBus::Valve, HfFunctionalGpioPin::SPI2_CS_TLE92466, flux::board::kTle92466SpiHz,
                         flux::board::kTle92466SpiMode, "TLE92466ED");
    } else {
        Logger::GetInstance().Error(TAG, "Valve SPI bus initialization failed (pins or controller)");
    }

    //==========================================================================
    // PRESSURE SPI BUS — dedicated to the ADS7952 external ADC (pin map SPI3_*)
    //==========================================================================
#ifdef HARDFOC_ADS7952_SUPPORT
    spi3_bus_valid_ = CreateSpiBusFromPins(SpiBus::Pressure, HfFunctionalGpioPin::SPI3_MISO,
                                           HfFunctionalGpioPin::SPI3_MOSI, HfFunctionalGpioPin::SPI3_SCK,
                                           flux::board::kPressureSpiController,
                                           flux::board::kPressureSpiUseIomux);
    if (spi3_bus_valid_) {
        spi_devices_[static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0)] =
            AddSpiDevice(SpiBus::Pressure, HfFunctionalGpioPin::SPI3_CS_ADS7952_0, flux::board::kAds7952SpiHz,
                         flux::board::kAds7952SpiMode, "ADS7952");
        spi3_bus_valid_ = spi_devices_[static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0)] != nullptr;
    } else {
        Logger::GetInstance().Warn(TAG, "Pressure SPI bus initialization failed — ADS7952 unavailable");
    }
#endif  // HARDFOC_ADS7952_SUPPORT

    //==========================================================================
    // CAN (TWAI) BUS — external devices (compressor)
    //==========================================================================
    {
        auto* tx_map = GetGpioMapping(HfFunctionalGpioPin::TWAI_TX);
        auto* rx_map = GetGpioMapping(HfFunctionalGpioPin::TWAI_RX);

        if (tx_map && rx_map) {
            const flux::bsp::CanSpec can_spec{static_cast<hf_pin_num_t>(tx_map->physical_pin),
                                              static_cast<hf_pin_num_t>(rx_map->physical_pin),
                                              flux::board::kCanBitrate};
            can_bus_ = flux::bsp::CreateCan(can_spec);
            if (can_bus_ != nullptr) {
                can_bus_valid_ = true;
                Logger::GetInstance().Info(TAG, "CAN bus configured (TX=GPIO%d, RX=GPIO%d, %lu bit/s)",
                                           tx_map->physical_pin, rx_map->physical_pin,
                                           static_cast<unsigned long>(flux::board::kCanBitrate));
            }
        }
    }

    Logger::GetInstance().Info(TAG, "Flux V1 comm channels initialized (SPI=%s, CAN=%s)",
                              spi_bus_valid_ ? "OK" : "FAIL",
                              can_bus_valid_ ? "OK" : "FAIL");

    // Fail closed on the required set: the valve bus with both valve chip selects.
    const bool valve_devices_ok =
        spi_devices_[static_cast<uint8_t>(SpiDeviceId::MAX22200_SOLENOID_DRIVER)] != nullptr &&
        spi_devices_[static_cast<uint8_t>(SpiDeviceId::TLE92466_LOWSIDE_DRIVER)] != nullptr;
    if (!spi_bus_valid_ || !valve_devices_ok) {
        Logger::GetInstance().Error(TAG, "Comms NOT initialised (fail-closed): valve SPI bus=%s, valve devices=%s",
                                    spi_bus_valid_ ? "OK" : "FAIL", valve_devices_ok ? "OK" : "FAIL");
        ReleaseLocked();
        return false;
    }
    return true;
}

void CommChannelsManager::ReleaseLocked() noexcept {
    flux::bsp::DestroySpiBus(static_cast<size_t>(flux::board::SpiBus::Valve));
    flux::bsp::DestroySpiBus(static_cast<size_t>(flux::board::SpiBus::Pressure));
    spi_devices_.fill(nullptr);
    spi_bus_valid_ = false;
    spi3_bus_valid_ = false;
    flux::bsp::DestroyCan();
    can_bus_ = nullptr;
    can_bus_valid_ = false;
}

//==============================================================================
// SPI ACCESSORS
//==============================================================================

BaseSpi* CommChannelsManager::GetSpiDevice(SpiDeviceId device_id) noexcept {
    return GetSpiDevice(static_cast<uint8_t>(device_id));
}

BaseSpi* CommChannelsManager::GetSpiDevice(uint8_t device_index) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return nullptr;
    if (device_index >= kSpiDeviceCount) return nullptr;
    return spi_devices_[device_index];
}

//==============================================================================
// CAN ACCESSOR
//==============================================================================

BaseCan* CommChannelsManager::GetCanBus() noexcept {
    if (!initialized_.load(std::memory_order_acquire) || !can_bus_valid_) return nullptr;
    return can_bus_;
}

//==============================================================================
// UART ACCESSORS — lazy-acquire pool (one slot per ESP32 hardware port).
//
// Pre-allocating UART ports at HAL bring-up (the way SPI / CAN are done)
// would force the HAL to know app-level pin assignments per product. The
// `AcquireUart` lazy pattern keeps the HAL board-agnostic for serial
// peripherals while still owning the port: apps describe which port +
// pins + baud they want, the HAL constructs and caches the UART
// instance, and apps consume the returned `BaseUart&` for their device
// driver. RS-485 half-duplex is enabled automatically when `rts_pin >=
// 0`.
//==============================================================================

BaseUart* CommChannelsManager::AcquireUart(const UartAcquireConfig& cfg) noexcept {
    if (cfg.port_number >= kMaxUartPorts) {
        Logger::GetInstance().Error(TAG, "AcquireUart: port_number %u out of range",
                                    static_cast<unsigned>(cfg.port_number));
        UpdateLastError(CommError::INVALID_PARAMETER);
        return nullptr;
    }

    MutexLockGuard lock(mutex_);

    // Already acquired? Return cached instance (config changes ignored).
    if (uarts_[cfg.port_number] != nullptr) {
        return uarts_[cfg.port_number];
    }

    const flux::bsp::UartSpec spec{cfg.port_number,
                                   static_cast<hf_pin_num_t>(cfg.tx_pin),
                                   static_cast<hf_pin_num_t>(cfg.rx_pin),
                                   static_cast<hf_pin_num_t>(cfg.rts_pin),
                                   static_cast<hf_pin_num_t>(cfg.cts_pin),
                                   cfg.baud_bps,
                                   cfg.tx_buffer_size,
                                   cfg.rx_buffer_size};
    bool rs485 = false;
    BaseUart* u = flux::bsp::CreateUart(spec, &rs485);
    if (u == nullptr) {
        Logger::GetInstance().Error(TAG, "AcquireUart: UART init failed (port=%u tx=%d rx=%d baud=%lu)",
                                    static_cast<unsigned>(cfg.port_number),
                                    static_cast<int>(cfg.tx_pin),
                                    static_cast<int>(cfg.rx_pin),
                                    static_cast<unsigned long>(cfg.baud_bps));
        UpdateLastError(CommError::INITIALIZATION_FAILED);
        return nullptr;
    }

    if (cfg.rts_pin >= 0) {
        if (!rs485) {
            Logger::GetInstance().Warn(TAG, "AcquireUart: RS-485 mode switch failed for port %u (continuing as plain UART)",
                                       static_cast<unsigned>(cfg.port_number));
        } else {
            Logger::GetInstance().Info(TAG, "UART%u in RS-485 half-duplex (DE/RE=GPIO%d) @ %lu bps",
                                       static_cast<unsigned>(cfg.port_number),
                                       static_cast<int>(cfg.rts_pin),
                                       static_cast<unsigned long>(cfg.baud_bps));
        }
    } else {
        Logger::GetInstance().Info(TAG, "UART%u plain mode TX=%d RX=%d @ %lu bps",
                                   static_cast<unsigned>(cfg.port_number),
                                   static_cast<int>(cfg.tx_pin),
                                   static_cast<int>(cfg.rx_pin),
                                   static_cast<unsigned long>(cfg.baud_bps));
    }

    uarts_[cfg.port_number] = u;
    return u;
}

BaseUart* CommChannelsManager::GetUart(uint8_t port_number) noexcept {
    if (port_number >= kMaxUartPorts) return nullptr;
    return uarts_[port_number];
}

//==============================================================================
// USB Serial/JTAG (lazy-acquire; underlying controller is a singleton)
//==============================================================================

BaseUsbSerialJtag* CommChannelsManager::AcquireUsbSerialJtag(
    const hf_usb_serial_jtag_config_t& cfg) noexcept {
    // The controller is one silicon block: first call wins the configuration and
    // later callers get the same instance. When the feature is compiled out
    // (HF_CORE_ENABLE_USB_SERIAL_JTAG=OFF) the binding returns nullptr and the
    // app falls back to its UART path without an #ifdef of its own.
    BaseUsbSerialJtag* inst = flux::bsp::AcquireUsbSerialJtag(cfg);
#ifdef HARDFOC_USB_SERIAL_JTAG_SUPPORT
    if (inst == nullptr) {
        Logger::GetInstance().Error(TAG, "AcquireUsbSerialJtag: driver install failed");
        UpdateLastError(CommError::INITIALIZATION_FAILED);
        return nullptr;
    }
#endif
    usb_serial_jtag_ = inst;
    return usb_serial_jtag_;
}

BaseUsbSerialJtag* CommChannelsManager::GetUsbSerialJtag() noexcept {
    return usb_serial_jtag_;
}

//==============================================================================
// DIAGNOSTICS
//==============================================================================

void CommChannelsManager::DumpStatistics() const noexcept {
    auto& log = Logger::GetInstance();
    log.Info(TAG, "=== Flux CommChannels Statistics ===");
    log.Info(TAG, "  SPI bus: %s", spi_bus_valid_ ? "OK" : "NOT CONFIGURED");
    log.Info(TAG, "  Pressure SPI bus: %s", spi3_bus_valid_ ? "OK" : "NOT CONFIGURED");
    for (uint8_t i = 0; i < kSpiDeviceCount; ++i) {
        log.Info(TAG, "  SPI device %d: %s", i, spi_devices_[i] != nullptr ? "OK" : "MISSING");
    }
    log.Info(TAG, "  I2C bus: not used on Flux V1");
    log.Info(TAG, "  CAN bus: %s", can_bus_valid_ ? "OK" : "NOT CONFIGURED");
    log.Info(TAG, "  USB Serial/JTAG: %s%s",
             usb_serial_jtag_ ? "ACQUIRED" : "NOT CONFIGURED",
             (usb_serial_jtag_ && usb_serial_jtag_->IsHostConnected()) ? " (host connected)" : "");
    log.Info(TAG, "=== End Flux CommChannels Statistics ===");
}

CommError CommChannelsManager::GetSystemDiagnostics(CommSystemDiagnostics& diagnostics) const noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return CommError::NOT_INITIALIZED;
    }

    MutexLockGuard lock(mutex_);

    diagnostics.system_initialized = true;
    diagnostics.spi_bus_valid = spi_bus_valid_;
    diagnostics.i2c_bus_valid = false;
    diagnostics.can_bus_valid = can_bus_valid_;
    diagnostics.last_error = last_error_.load(std::memory_order_acquire);

    // Count valid SPI devices
    diagnostics.spi_device_count = 0;
    for (uint8_t i = 0; i < kSpiDeviceCount; ++i) {
        if (spi_devices_[i] != nullptr) ++diagnostics.spi_device_count;
    }

    diagnostics.system_healthy = spi_bus_valid_; // SPI is the hard requirement for Flux
    return CommError::SUCCESS;
}

//==============================================================================
// DEINITIALIZE
//==============================================================================

bool CommChannelsManager::Deinitialize() noexcept {
    MutexLockGuard lock(mutex_);
    if (!initialized_.load(std::memory_order_acquire)) return true;

    Logger::GetInstance().Info(TAG, "Deinitializing Flux CommChannels manager");

    // Release both SPI buses, their devices and the CAN controller. UART ports stay:
    // they are acquired by the application, which owns their lifetime.
    ReleaseLocked();

    initialized_.store(false, std::memory_order_release);
    manager_error_.store(flux::hal::ManagerError::NotInitialized, std::memory_order_release);
    return true;
}
