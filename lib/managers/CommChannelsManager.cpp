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
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspI2c.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/mcu/esp32/EspCan.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/utils/memory_utils.h"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"
#include "handlers/logger/Logger.h"

static constexpr const char* TAG = "FluxComm";

// Destructor defined here where EspSpiBus, EspI2cBus, EspCan are complete types
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

    //==========================================================================
    // SPI BUS (SPI2_HOST) — shared by MAX22200 and TLE92466ED
    //==========================================================================
    {
        auto* miso_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_MISO);
        auto* mosi_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_MOSI);
        auto* sck_map  = GetGpioMapping(HfFunctionalGpioPin::SPI2_SCK);

        if (miso_map && mosi_map && sck_map) {
            hf_spi_bus_config_t spi_cfg = {};
            spi_cfg.host      = static_cast<hf_host_id_t>(1); // SPI2_HOST
            spi_cfg.miso_pin  = miso_map->physical_pin;
            spi_cfg.mosi_pin  = mosi_map->physical_pin;
            spi_cfg.sclk_pin  = sck_map->physical_pin;
            spi_cfg.dma_channel = 0;   // auto-select
            spi_cfg.timeout_ms  = 1000;
            spi_cfg.use_iomux   = true;

            spi_bus_ = hf::utils::make_unique_nothrow<EspSpiBus>(spi_cfg);
            if (spi_bus_ && spi_bus_->Initialize()) {
                spi_bus_valid_ = true;

                // Device 0: MAX22200 — SPI Mode 0, 10 MHz
                auto* cs_max_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_CS_MAX22200);
                if (cs_max_map) {
                    hf_spi_device_config_t dev_cfg = {};
                    dev_cfg.cs_pin          = cs_max_map->physical_pin;
                    dev_cfg.clock_speed_hz  = 10'000'000;
                    dev_cfg.mode            = hf_spi_mode_t::HF_SPI_MODE_0;
                    int idx = spi_bus_->CreateDevice(dev_cfg);
                    spi_device_indices_[static_cast<uint8_t>(SpiDeviceId::MAX22200_SOLENOID_DRIVER)] = idx;
                    Logger::GetInstance().Info(TAG, "MAX22200 SPI device added (CS=GPIO%d, idx=%d)",
                                              cs_max_map->physical_pin, idx);
                }

                // Device 1: TLE92466ED — SPI Mode 1, 5 MHz
                auto* cs_tle_map = GetGpioMapping(HfFunctionalGpioPin::SPI2_CS_TLE92466);
                if (cs_tle_map) {
                    hf_spi_device_config_t dev_cfg = {};
                    dev_cfg.cs_pin          = cs_tle_map->physical_pin;
                    dev_cfg.clock_speed_hz  = 5'000'000;
                    dev_cfg.mode            = hf_spi_mode_t::HF_SPI_MODE_1;
                    int idx = spi_bus_->CreateDevice(dev_cfg);
                    spi_device_indices_[static_cast<uint8_t>(SpiDeviceId::TLE92466_LOWSIDE_DRIVER)] = idx;
                    Logger::GetInstance().Info(TAG, "TLE92466ED SPI device added (CS=GPIO%d, idx=%d)",
                                              cs_tle_map->physical_pin, idx);
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
    // SPI3 BUS (SPI3_HOST) — dedicated bus for ADS7952 external ADC
    //==========================================================================
#ifdef HARDFOC_ADS7952_SUPPORT
    {
        auto* miso_map = GetGpioMapping(HfFunctionalGpioPin::SPI3_MISO);
        auto* mosi_map = GetGpioMapping(HfFunctionalGpioPin::SPI3_MOSI);
        auto* sck_map  = GetGpioMapping(HfFunctionalGpioPin::SPI3_SCK);

        if (miso_map && mosi_map && sck_map) {
            hf_spi_bus_config_t spi3_cfg = {};
            spi3_cfg.host      = static_cast<hf_host_id_t>(2); // SPI3_HOST
            spi3_cfg.miso_pin  = miso_map->physical_pin;
            spi3_cfg.mosi_pin  = mosi_map->physical_pin;
            spi3_cfg.sclk_pin  = sck_map->physical_pin;
            spi3_cfg.dma_channel = 0;   // auto-select
            spi3_cfg.timeout_ms  = 1000;
            spi3_cfg.use_iomux   = false;

            spi3_bus_ = hf::utils::make_unique_nothrow<EspSpiBus>(spi3_cfg);
            if (spi3_bus_ && spi3_bus_->Initialize()) {
                spi3_bus_valid_ = true;

                // Device 0: ADS7952 — SPI Mode 0, 20 MHz (ADS7952 supports up to 20 MHz)
                auto* cs_adc_map = GetGpioMapping(HfFunctionalGpioPin::SPI3_CS_ADS7952_0);
                if (cs_adc_map) {
                    hf_spi_device_config_t dev_cfg = {};
                    dev_cfg.cs_pin          = cs_adc_map->physical_pin;
                    dev_cfg.clock_speed_hz  = 20'000'000;
                    dev_cfg.mode            = hf_spi_mode_t::HF_SPI_MODE_0;
                    int idx = spi3_bus_->CreateDevice(dev_cfg);
                    spi_device_indices_[static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0)] = idx;
                    spi_device_bus_[static_cast<uint8_t>(SpiDeviceId::ADS7952_ADC_0)] = 1; // SPI3
                    Logger::GetInstance().Info(TAG, "ADS7952 SPI device added (CS=GPIO%d, idx=%d, SPI3)",
                                              cs_adc_map->physical_pin, idx);
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

    //==========================================================================
    // I2C BUS — future expansion (e.g., EEPROM, additional sensors)
    //==========================================================================
    {
        auto* sda_map = GetGpioMapping(HfFunctionalGpioPin::I2C_SDA);
        auto* scl_map = GetGpioMapping(HfFunctionalGpioPin::I2C_SCL);

        if (sda_map && scl_map) {
            hf_i2c_master_bus_config_t i2c_cfg = {};
            i2c_cfg.i2c_port   = I2C_NUM_0;
            i2c_cfg.sda_io_num = static_cast<hf_pin_num_t>(sda_map->physical_pin);
            i2c_cfg.scl_io_num = static_cast<hf_pin_num_t>(scl_map->physical_pin);

            i2c_bus_ = hf::utils::make_unique_nothrow<EspI2cBus>(i2c_cfg);
            if (i2c_bus_ && i2c_bus_->Initialize()) {
                i2c_bus_valid_   = true;
                Logger::GetInstance().Info(TAG, "I2C bus configured (SDA=GPIO%d, SCL=GPIO%d, 400kHz)",
                                          sda_map->physical_pin, scl_map->physical_pin);
            } else {
                Logger::GetInstance().Warn(TAG, "I2C bus init failed — non-critical for Flux V1");
                i2c_bus_.reset();
            }
        }
    }

    Logger::GetInstance().Info(TAG, "Flux V1 comm channels initialized (SPI=%s, CAN=%s, I2C=%s)",
                              spi_bus_valid_ ? "OK" : "FAIL",
                              can_bus_valid_ ? "OK" : "FAIL",
                              i2c_bus_valid_ ? "OK" : "FAIL");

    // If SPI failed (hard requirement), clean up any non-critical allocations
    if (!spi_bus_valid_) {
        can_bus_.reset();
        can_bus_valid_ = false;
        i2c_bus_.reset();
        i2c_bus_valid_ = false;
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
// I2C / CAN ACCESSORS
//==============================================================================

EspI2cBus* CommChannelsManager::GetI2cBus() noexcept {
    if (!initialized_.load(std::memory_order_acquire) || !i2c_bus_valid_) return nullptr;
    return i2c_bus_.get();
}

BaseCan* CommChannelsManager::GetCanBus() noexcept {
    if (!initialized_.load(std::memory_order_acquire) || !can_bus_valid_) return nullptr;
    return can_bus_.get();
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
    log.Info(TAG, "  I2C bus: %s", i2c_bus_valid_ ? "OK" : "NOT CONFIGURED");
    log.Info(TAG, "  CAN bus: %s", can_bus_valid_ ? "OK" : "NOT CONFIGURED");
    log.Info(TAG, "=== End Flux CommChannels Statistics ===");
}

CommError CommChannelsManager::GetSystemDiagnostics(CommSystemDiagnostics& diagnostics) const noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return CommError::NOT_INITIALIZED;
    }

    MutexLockGuard lock(mutex_);

    diagnostics.system_initialized = true;
    diagnostics.spi_bus_valid = spi_bus_valid_;
    diagnostics.i2c_bus_valid = i2c_bus_valid_;
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

    // Release I2C bus
    i2c_bus_.reset();
    i2c_bus_valid_ = false;

    // Release CAN bus
    can_bus_.reset();
    can_bus_valid_ = false;

    initialized_.store(false, std::memory_order_release);
    return true;
}
