/**
 * @file CommChannelsManager.cpp
 * @brief Implementation of the Flux V1 communication channels manager.
 *
 * @author HardFOC Team
 * @date 2026
 * @version 1.0
 */

#include "CommChannelsManager.h"

#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspSpi.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspCan.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspUart.h"
#ifdef HARDFOC_USB_SERIAL_JTAG_SUPPORT
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspUsbSerialJtag.h"
#endif
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/memory_utils.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"
#include "handlers/logger/Logger.h"

static constexpr const char* TAG = "FluxComm";

// Destructor defined here where EspSpiBus, EspCan, EspUart are complete types
CommChannelsManager::~CommChannelsManager() = default;

//==============================================================================
// SINGLETON
//==============================================================================

CommChannelsManager& CommChannelsManager::GetInstance() noexcept {
    static CommChannelsManager instance;
    return instance;
}

CommChannelsManager::CommChannelsManager() noexcept {
    spi_device_indices_.fill(-1);
    spi_device_bus_.fill(0);
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
    initialized_.store(ok, std::memory_order_release);
    return ok;
}

bool CommChannelsManager::Initialize() noexcept {
    Logger::GetInstance().Info(TAG, "Initializing Flux V1 communication channels");

    struct SpiSpeedDisplay {
        uint32_t value;
        const char* unit;
    };
    const auto format_spi_speed = [](uint32_t hz) noexcept -> SpiSpeedDisplay {
        if (hz >= 1'000'000U) {
            return {hz / 1'000'000U, "MHz"};
        }
        return {hz / 1'000U, "kHz"};
    };

    //==========================================================================
    // SPI BUS (SPI3_HOST) — shared by MAX22200 and TLE92466ED
    // Note: GPIO35-38 use SPI3_HOST via GPIO matrix (SPI3 has no IOMUX pins)
    //==========================================================================
    {
        auto* miso_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_MISO);
        auto* mosi_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_MOSI);
        auto* sck_map  = GetGpioMapping(HfFunctionalGpioPin::SPI2_SCK);

        if (miso_map && mosi_map && sck_map) {
            hf_spi_bus_config_t spi_cfg = {};
            spi_cfg.host      = static_cast<hf_host_id_t>(2); // SPI3_HOST (GPIO matrix, no IOMUX)
            spi_cfg.miso_pin  = miso_map->physical_pin;
            spi_cfg.mosi_pin  = mosi_map->physical_pin;
            spi_cfg.sclk_pin  = sck_map->physical_pin;
            spi_cfg.dma_channel = 0;   // auto-select
            spi_cfg.timeout_ms  = 1000;
            spi_cfg.use_iomux   = false; // GPIO matrix for SPI3

            spi_bus_ = hf::utils::make_unique_nothrow<EspSpiBus>(spi_cfg);
            if (spi_bus_ && spi_bus_->Initialize()) {
                spi_bus_valid_ = true;

                // Device 0: MAX22200 — SPI Mode 0, 1 MHz (matches the
                // tested driver-level example which is known to enumerate
                // the chip reliably; 10 MHz is the absolute datasheet max
                // and demands very tight signal integrity that the dev
                // harness rarely meets).
                auto* cs_max_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_CS_MAX22200);
                if (cs_max_map) {
                    hf_spi_device_config_t dev_cfg = {};
                    dev_cfg.cs_pin          = cs_max_map->physical_pin;
                    dev_cfg.clock_speed_hz  = 1'000'000;
                    dev_cfg.mode            = hf_spi_mode_t::HF_SPI_MODE_0;
                    int idx = spi_bus_->CreateDevice(dev_cfg);
                    const SpiSpeedDisplay speed = format_spi_speed(static_cast<uint32_t>(dev_cfg.clock_speed_hz));
                    spi_device_indices_[static_cast<uint8_t>(SpiDeviceId::MAX22200_SOLENOID_DRIVER)] = idx;
                    Logger::GetInstance().Info(TAG,
                                               "MAX22200 SPI device added (host=%d SPI3_HOST, CS=GPIO%d, idx=%d, speed=%u %s, mode=%d)",
                                               static_cast<int>(spi_cfg.host), cs_max_map->physical_pin, idx,
                                               speed.value, speed.unit, static_cast<int>(dev_cfg.mode));
                }

                // Device 1: TLE92466ED — SPI Mode 1, 1 MHz (lowered from
                // 5 MHz to match the proven driver-level example;
                // datasheet allows up to 8 MHz but only with a tightly
                // controlled bus).
                auto* cs_tle_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_CS_TLE92466);
                if (cs_tle_map) {
                    hf_spi_device_config_t dev_cfg = {};
                    dev_cfg.cs_pin          = cs_tle_map->physical_pin;
                    dev_cfg.clock_speed_hz  = 1'000'000;
                    dev_cfg.mode            = hf_spi_mode_t::HF_SPI_MODE_1;
                    int idx = spi_bus_->CreateDevice(dev_cfg);
                    const SpiSpeedDisplay speed = format_spi_speed(static_cast<uint32_t>(dev_cfg.clock_speed_hz));
                    spi_device_indices_[static_cast<uint8_t>(SpiDeviceId::TLE92466_LOWSIDE_DRIVER)] = idx;
                    Logger::GetInstance().Info(TAG,
                                               "TLE92466ED SPI device added (host=%d SPI3_HOST, CS=GPIO%d, idx=%d, speed=%u %s, mode=%d)",
                                               static_cast<int>(spi_cfg.host), cs_tle_map->physical_pin, idx,
                                               speed.value, speed.unit, static_cast<int>(dev_cfg.mode));
                }
            } else {
                Logger::GetInstance().Error(TAG, "SPI bus initialization failed");
                spi_bus_.reset();
            }
        } else {
            Logger::GetInstance().Error(TAG, "SPI pin mapping missing");
        }
    }

    //==========================================================================
    // SPI2 BUS (SPI2_HOST) — dedicated bus for ADS7952 external ADC
    // Note: GPIO10-13 are native IOMUX pins for SPI2_HOST on ESP32
    //==========================================================================
#ifdef HARDFOC_ADS7952_SUPPORT
    {
        auto* miso_map = GetGpioMapping(HfFunctionalGpioPin::SPI3_MISO);
        auto* mosi_map = GetGpioMapping(HfFunctionalGpioPin::SPI3_MOSI);
        auto* sck_map  = GetGpioMapping(HfFunctionalGpioPin::SPI3_SCK);

        if (miso_map && mosi_map && sck_map) {
            hf_spi_bus_config_t spi3_cfg = {};
            spi3_cfg.host      = static_cast<hf_host_id_t>(1); // SPI2_HOST (GPIO10-13 are native IOMUX)
            spi3_cfg.miso_pin  = miso_map->physical_pin;
            spi3_cfg.mosi_pin  = mosi_map->physical_pin;
            spi3_cfg.sclk_pin  = sck_map->physical_pin;
            spi3_cfg.dma_channel = 0;   // auto-select
            spi3_cfg.timeout_ms  = 1000;
            spi3_cfg.use_iomux   = true; // Native IOMUX for SPI2 on GPIO10-13

            spi3_bus_ = hf::utils::make_unique_nothrow<EspSpiBus>(spi3_cfg);
            if (spi3_bus_ && spi3_bus_->Initialize()) {
                spi3_bus_valid_ = true;

                // Device 0: ADS7952 — SPI Mode 0, 20 MHz (ADS7952 supports up to 20 MHz)
                auto* cs_adc_map = GetGpioMapping(HfFunctionalGpioPin::SPI3_CS_ADS7952_0);
                if (cs_adc_map) {
                    hf_spi_device_config_t dev_cfg = {};
                    dev_cfg.cs_pin          = cs_adc_map->physical_pin;
                    dev_cfg.clock_speed_hz  = 4'000'000;  // 4 MHz for ADS7952 stability
                    dev_cfg.mode            = hf_spi_mode_t::HF_SPI_MODE_0;
                    int idx = spi3_bus_->CreateDevice(dev_cfg);
                    const SpiSpeedDisplay speed = format_spi_speed(static_cast<uint32_t>(dev_cfg.clock_speed_hz));
                    spi_device_indices_[static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0)] = idx;
                    spi_device_bus_[static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0)] = 1; // SPI3
                    Logger::GetInstance().Info(TAG,
                                               "ADS7952 SPI device added (host=%d SPI2_HOST, CS=GPIO%d, idx=%d, speed=%u %s, mode=%d)",
                                               static_cast<int>(spi3_cfg.host), cs_adc_map->physical_pin, idx,
                                               speed.value, speed.unit, static_cast<int>(dev_cfg.mode));
                }
            } else {
                Logger::GetInstance().Warn(TAG, "SPI3 bus initialization failed — ADS7952 unavailable");
                spi3_bus_.reset();
            }
        } else {
            Logger::GetInstance().Warn(TAG, "SPI3 pin mapping missing — ADS7952 unavailable");
        }
    }
#endif // HARDFOC_ADS7952_SUPPORT

    //==========================================================================
    // CAN (TWAI) BUS — for valve network communication
    //==========================================================================
    {
        auto* tx_map = GetGpioMapping(HfFunctionalGpioPin::TWAI_TX);
        auto* rx_map = GetGpioMapping(HfFunctionalGpioPin::TWAI_RX);

        if (tx_map && rx_map) {
            hf_esp_can_config_t can_cfg = {};
            can_cfg.tx_pin    = tx_map->physical_pin;
            can_cfg.rx_pin    = rx_map->physical_pin;
            can_cfg.baud_rate = 500000; // 500 kbps standard

            can_bus_ = hf::utils::make_unique_nothrow<EspCan>(can_cfg);
            if (can_bus_) {
                can_bus_valid_   = true;
                Logger::GetInstance().Info(TAG, "CAN bus configured (TX=GPIO%d, RX=GPIO%d, 500kbps)",
                                          tx_map->physical_pin, rx_map->physical_pin);
            }
        }
    }

    Logger::GetInstance().Info(TAG, "Flux V1 comm channels initialized (SPI=%s, CAN=%s)",
                              spi_bus_valid_ ? "OK" : "FAIL",
                              can_bus_valid_ ? "OK" : "FAIL");

    // If SPI failed (hard requirement), clean up any non-critical allocations
    if (!spi_bus_valid_) {
        can_bus_.reset();
        can_bus_valid_ = false;
    }

    return spi_bus_valid_; // SPI is the only hard requirement
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
    int idx = spi_device_indices_[device_index];
    if (idx < 0) return nullptr;

    EspSpiBus* bus = GetBusForDevice(device_index);
    if (!bus) return nullptr;
    return bus->GetDevice(idx);
}

EspSpiBus* CommChannelsManager::GetBusForDevice(uint8_t device_index) noexcept {
    if (device_index >= kSpiDeviceCount) return nullptr;
    uint8_t bus_id = spi_device_bus_[device_index];
    if (bus_id == 0) return spi_bus_valid_ ? spi_bus_.get() : nullptr;
    if (bus_id == 1) return spi3_bus_valid_ ? spi3_bus_.get() : nullptr;
    return nullptr;
}

//==============================================================================
// CAN ACCESSOR
//==============================================================================

BaseCan* CommChannelsManager::GetCanBus() noexcept {
    if (!initialized_.load(std::memory_order_acquire) || !can_bus_valid_) return nullptr;
    return can_bus_.get();
}

//==============================================================================
// UART ACCESSORS — lazy-acquire pool (one slot per ESP32 hardware port).
//
// Pre-allocating UART ports at HAL bring-up (the way SPI / CAN are done)
// would force the HAL to know app-level pin assignments per product. The
// `AcquireUart` lazy pattern keeps the HAL board-agnostic for serial
// peripherals while still owning the port: apps describe which port +
// pins + baud they want, the HAL constructs and caches the EspUart
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
    if (uarts_[cfg.port_number]) {
        return uarts_[cfg.port_number].get();
    }

    hf_uart_config_t uart_cfg{};
    uart_cfg.port_number    = static_cast<hf_port_num_t>(cfg.port_number);
    uart_cfg.baud_rate      = cfg.baud_bps;
    uart_cfg.tx_pin         = static_cast<hf_pin_num_t>(cfg.tx_pin);
    uart_cfg.rx_pin         = static_cast<hf_pin_num_t>(cfg.rx_pin);
    uart_cfg.rts_pin        = static_cast<hf_pin_num_t>(cfg.rts_pin);
    uart_cfg.cts_pin        = static_cast<hf_pin_num_t>(cfg.cts_pin);
    uart_cfg.data_bits      = hf_uart_data_bits_t::HF_UART_DATA_8_BITS;
    uart_cfg.parity         = hf_uart_parity_t::HF_UART_PARITY_DISABLE;
    uart_cfg.stop_bits      = hf_uart_stop_bits_t::HF_UART_STOP_BITS_1;
    uart_cfg.flow_control   = hf_uart_flow_ctrl_t::HF_UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.tx_buffer_size = cfg.tx_buffer_size;
    uart_cfg.rx_buffer_size = cfg.rx_buffer_size;

    auto u = hf::utils::make_unique_nothrow<EspUart>(uart_cfg);
    if (!u || !u->EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "AcquireUart: EspUart init failed (port=%u tx=%d rx=%d baud=%lu)",
                                    static_cast<unsigned>(cfg.port_number),
                                    static_cast<int>(cfg.tx_pin),
                                    static_cast<int>(cfg.rx_pin),
                                    static_cast<unsigned long>(cfg.baud_bps));
        UpdateLastError(CommError::INITIALIZATION_FAILED);
        return nullptr;
    }

    if (cfg.rts_pin >= 0) {
        const auto rs485_err = u->SetCommunicationMode(hf_uart_mode_t::HF_UART_MODE_RS485);
        if (rs485_err != hf_uart_err_t::UART_SUCCESS) {
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

    uarts_[cfg.port_number] = std::move(u);
    return uarts_[cfg.port_number].get();
}

BaseUart* CommChannelsManager::GetUart(uint8_t port_number) noexcept {
    if (port_number >= kMaxUartPorts) return nullptr;
    return uarts_[port_number].get();
}

//==============================================================================
// USB Serial/JTAG (lazy-acquire; underlying controller is a singleton)
//==============================================================================

BaseUsbSerialJtag* CommChannelsManager::AcquireUsbSerialJtag(
    const hf_usb_serial_jtag_config_t& cfg) noexcept {
#ifdef HARDFOC_USB_SERIAL_JTAG_SUPPORT
    // Magic-static singleton — first call wins the configuration. Subsequent
    // callers get the same instance regardless of `cfg` (matches the fact
    // that the silicon block itself is global to the chip).
    auto& inst = EspUsbSerialJtag::Default(cfg);
    if (!inst.EnsureInitialized()) {
        Logger::GetInstance().Error(TAG, "AcquireUsbSerialJtag: driver install failed");
        UpdateLastError(CommError::INITIALIZATION_FAILED);
        return nullptr;
    }
    usb_serial_jtag_ = &inst;
    return usb_serial_jtag_;
#else
    // Feature disabled at HAL CMake level (HF_CORE_ENABLE_USB_SERIAL_JTAG=OFF).
    // The API is intentionally still callable so app code doesn't need its
    // own #ifdef — it just gets a nullptr and falls back to its UART path.
    (void)cfg;
    return nullptr;
#endif
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
    for (uint8_t i = 0; i < kSpiDeviceCount; ++i) {
        log.Info(TAG, "  SPI device %d: idx=%d", i, spi_device_indices_[i]);
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
        if (spi_device_indices_[i] >= 0) ++diagnostics.spi_device_count;
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

    // Release SPI bus
    spi_bus_.reset();
    spi_bus_valid_ = false;

    // Release SPI3 bus (ADS7952)
    spi3_bus_.reset();
    spi3_bus_valid_ = false;

    // Release CAN bus
    can_bus_.reset();
    can_bus_valid_ = false;

    initialized_.store(false, std::memory_order_release);
    return true;
}
