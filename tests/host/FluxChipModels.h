/**
 * @file FluxChipModels.h
 * @brief Register-level SPI models of the Flux V1 chips, for the HAL host tests.
 *
 * @details Each model is a `MockSpi` responder that answers the real hf-core handler and chip
 *          driver the way the part does on the wire, so `ValveManager` and `AdcManager` run their
 *          unmodified bring-up and channel code against them:
 *
 *          - @ref Max22200Model: two-phase protocol (8-bit command with CMD high, then 8- or
 *            32-bit data with CMD low), STATUS/CFG/FAULT register file, fault flags on SDO, and
 *            the CFG_CHx fields as the chip reads them (VDR duty in percent).
 *          - @ref Tle92466Model: 32-bit frames with SAE J1850 CRC, one-frame reply pipeline,
 *            16-/22-bit reply widths per register, register file with ICVID, and critical-fault
 *            replies (reply mode 10B, no CRC).
 *          - @ref Ads7952Model: 16-bit frames, manual channel select, channel id in the reply.
 *
 *          They model what the HAL relies on, not the whole datasheet.
 *
 * @copyright HardFOC
 */

#pragma once

#include <array>
#include <cstdint>
#include <map>

#include "HfCoreMocks.h"
#include "bsp_bind/host/FluxHostBind.h"
#include "core/hf-core-drivers/external/hf-max22200-driver/inc/max22200_registers.hpp"
#include "core/hf-core-drivers/external/hf-tle92466ed-driver/inc/tle92466ed.hpp"
#include "core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp"

namespace flux::test {

/** @brief MCU pin number of a functional pin, from the pin map. */
inline hf_pin_num_t Pin(HfFunctionalGpioPin p) {
    const auto* m = GetGpioMapping(p);
    return m != nullptr ? static_cast<hf_pin_num_t>(m->physical_pin) : -1;
}

//==============================================================================
// MAX22200
//==============================================================================

class Max22200Model {
public:
    std::array<uint32_t, 11> regs{};  ///< STATUS, CFG_CH0..7, FAULT, CFG_DPM
    uint8_t fault_flags{0};           ///< STATUS[7:1] read-only flags (OVT, OCP, OLF, HHF, DPM, COMER, UVM)
    bool absent{false};               ///< MISO idle: every byte reads 0xFF
    int status_writes{0};

    void Respond(const uint8_t* tx, uint8_t* rx, uint16_t len) {
        if (absent) {
            for (uint16_t i = 0; i < len; ++i) rx[i] = 0xFF;
            return;
        }
        const auto* cmd_pin = flux::bsp::host::GpioByPin(Pin(HfFunctionalGpioPin::MAX22200_CMD));
        const bool cmd_high = cmd_pin != nullptr && cmd_pin->DrivenLevel() == hf_gpio_level_t::HF_GPIO_LEVEL_HIGH;
        if (cmd_high && len == 1) {
            cmd_ = tx[0];
            rx[0] = StatusByte0();
            return;
        }
        const uint8_t bank = static_cast<uint8_t>((cmd_ >> 1) & 0x0F);
        const bool write = (cmd_ & 0x80) != 0;
        if (bank >= regs.size()) {
            for (uint16_t i = 0; i < len; ++i) rx[i] = 0;
            return;
        }
        if (len == 4) {
            if (write) {
                const uint32_t v = static_cast<uint32_t>(tx[0]) | (static_cast<uint32_t>(tx[1]) << 8) |
                                   (static_cast<uint32_t>(tx[2]) << 16) | (static_cast<uint32_t>(tx[3]) << 24);
                Write(bank, v);
                rx[0] = StatusByte0();
                rx[1] = cmd_;
                rx[2] = 0;
                rx[3] = 0;
            } else {
                const uint32_t v = Read(bank);
                rx[0] = static_cast<uint8_t>(v >> 24);
                rx[1] = static_cast<uint8_t>(v >> 16);
                rx[2] = static_cast<uint8_t>(v >> 8);
                rx[3] = static_cast<uint8_t>(v);
            }
        } else if (len == 1) {
            if (write) {
                Write(bank, (regs[bank] & 0x00FFFFFFu) | (static_cast<uint32_t>(tx[0]) << 24));
                rx[0] = StatusByte0();
            } else {
                rx[0] = static_cast<uint8_t>(Read(bank) >> 24);
            }
        }
    }

    /** @brief ONCH bits (channels switched on). */
    uint8_t ChannelsOn() const { return static_cast<uint8_t>(regs[max22200::RegBank::STATUS] >> 24); }

    /** @brief CFG_CHx of channel @p ch (0-7). */
    uint32_t Cfg(uint8_t ch) const { return regs[max22200::RegBank::CFG_CH0 + ch]; }
    /** @brief VDRnCDR of channel @p ch: true in voltage drive. */
    bool IsVdr(uint8_t ch) const { return (Cfg(ch) & max22200::CfgChReg::VDRNCDR_BIT) != 0u; }
    /**
     * @brief HIT[6:0] / HOLD[6:0] as the chip reads them. In VDR the field is the duty in percent
     *        (datasheet Table 3: 40 % is 40, 100 and above run at 100 %); in CDR it is the current
     *        in 1/127 of IFS.
     */
    uint8_t HitField(uint8_t ch) const {
        return static_cast<uint8_t>((Cfg(ch) & max22200::CfgChReg::HIT_MASK) >> max22200::CfgChReg::HIT_SHIFT);
    }
    uint8_t HoldField(uint8_t ch) const {
        return static_cast<uint8_t>((Cfg(ch) & max22200::CfgChReg::HOLD_MASK) >> max22200::CfgChReg::HOLD_SHIFT);
    }

    void Install() {
        flux::bsp::host::SetSpiResponder(Pin(HfFunctionalGpioPin::SPI2_CS_MAX22200),
                                         [this](const uint8_t* tx, uint8_t* rx, uint16_t len) { Respond(tx, rx, len); });
    }

private:
    uint8_t cmd_{0};

    uint8_t StatusByte0() const {
        return static_cast<uint8_t>((regs[max22200::RegBank::STATUS] & max22200::StatusReg::ACTIVE_BIT) |
                                    (fault_flags & max22200::StatusReg::FAULT_FLAGS_MASK));
    }
    uint32_t Read(uint8_t bank) const {
        if (bank == max22200::RegBank::STATUS) {
            return (regs[bank] & ~max22200::StatusReg::FAULT_FLAGS_MASK) | (fault_flags & 0xFEu);
        }
        return regs[bank];
    }
    void Write(uint8_t bank, uint32_t v) {
        if (bank == max22200::RegBank::FAULT) {
            return;  // read-only
        }
        if (bank == max22200::RegBank::STATUS) {
            ++status_writes;
        }
        regs[bank] = v;
    }
};

//==============================================================================
// TLE92466ED
//==============================================================================

class Tle92466Model {
public:
    std::map<uint16_t, uint16_t> regs;  ///< Register file (write address == read address below 0x80)
    bool absent{false};                 ///< MISO idle (all zero)
    uint16_t icvid{0xC102};             ///< ICVID value (0xC1xx = TLE92466ED)
    /// Answer every frame with a critical-fault reply (datasheet 5.2.3.5): reply mode 10B, the cause in
    /// bits 7:0, and bits 31:24 don't care, not a CRC. The part sends it while a supply or its clock is
    /// out of range and cannot serve the access.
    bool critical_fault{false};
    uint8_t critical_fault_flags{0x01};

    Tle92466Model() { regs[tle92466ed::CentralReg::ICVID] = icvid; }

    void Respond(const uint8_t* tx, uint8_t* rx, uint16_t len) {
        if (len != 4) {
            for (uint16_t i = 0; i < len; ++i) rx[i] = 0;
            return;
        }
        const uint32_t out = absent ? 0U : pending_;
        rx[0] = static_cast<uint8_t>(out >> 24);
        rx[1] = static_cast<uint8_t>(out >> 16);
        rx[2] = static_cast<uint8_t>(out >> 8);
        rx[3] = static_cast<uint8_t>(out);

        tle92466ed::SPIFrame in{};
        in.word = (static_cast<uint32_t>(tx[0]) << 24) | (static_cast<uint32_t>(tx[1]) << 16) |
                  (static_cast<uint32_t>(tx[2]) << 8) | static_cast<uint32_t>(tx[3]);
        tle92466ed::SPIFrame reply{};
        if (in.tx_fields.rw == 1U) {
            const auto addr = static_cast<uint16_t>(in.tx_fields.address);
            const auto data = static_cast<uint16_t>(in.tx_fields.data);
            if (IsWriteOneToClear(addr)) {
                regs[addr] = static_cast<uint16_t>(Reg(addr) & ~data);  // diagnosis flags: write 1 to clear
            } else {
                regs[addr] = data;
            }
            reply.rx_16bit.data = data;
            reply.rx_16bit.rw_echo = 1;
            reply.rx_16bit.reply_mode = 0;
        } else {
            const auto addr = static_cast<uint16_t>(in.tx_fields.data);
            const auto it = regs.find(addr);
            const uint32_t value = (it != regs.end()) ? it->second : 0U;
            if (tle92466ed::ExpectedReplyFor(addr) == tle92466ed::ExpectedReply::Bits22) {
                reply.rx_22bit.data = value & 0x3FFFFFU;
                reply.rx_22bit.reply_mode = 1;
            } else {
                reply.rx_16bit.data = value & 0xFFFFU;
                reply.rx_16bit.reply_mode = 0;
            }
        }
        reply.rx_common.crc = tle92466ed::CalculateFrameCrc(reply);
        if (critical_fault) {
            reply.word = 0;
            reply.rx_fault.fault_flags = critical_fault_flags;
            reply.rx_fault.reply_mode = 0x2;
            reply.rx_fault._dont_care = 0x5A;  // deliberately not the frame's CRC
        }
        pending_ = reply.word;
    }

    uint16_t Reg(uint16_t addr) const {
        const auto it = regs.find(addr);
        return it != regs.end() ? it->second : 0;
    }

    void Install() {
        regs[tle92466ed::CentralReg::ICVID] = icvid;
        flux::bsp::host::SetSpiResponder(Pin(HfFunctionalGpioPin::SPI2_CS_TLE92466),
                                         [this](const uint8_t* tx, uint8_t* rx, uint16_t len) { Respond(tx, rx, len); });
    }

private:
    uint32_t pending_{0};

    static bool IsWriteOneToClear(uint16_t addr) {
        using namespace tle92466ed::CentralReg;
        return addr == GLOBAL_DIAG0 || addr == GLOBAL_DIAG1 || addr == GLOBAL_DIAG2 ||
               (addr >= DIAG_ERR_CHGR0 && addr < DIAG_ERR_CHGR0 + 3) ||
               (addr >= DIAG_WARN_CHGR0 && addr < DIAG_WARN_CHGR0 + 3);
    }
};

//==============================================================================
// ADS7952
//==============================================================================

class Ads7952Model {
public:
    std::array<uint16_t, 12> counts{};  ///< 12-bit conversion result per channel
    bool absent{false};                 ///< MISO idle: replies never carry the requested channel

    void Respond(const uint8_t* tx, uint8_t* rx, uint16_t len) {
        if (len != 2) {
            for (uint16_t i = 0; i < len; ++i) rx[i] = 0;
            return;
        }
        const uint16_t cmd = static_cast<uint16_t>((tx[0] << 8) | tx[1]);
        if ((cmd & 0xF000U) == 0x1000U) {  // MANUAL: select channel DI[10:7]
            channel_ = static_cast<uint8_t>((cmd >> 7) & 0x0FU);
        }
        uint16_t out = 0xFFFF;
        if (!absent && channel_ < counts.size()) {
            out = static_cast<uint16_t>((static_cast<uint16_t>(channel_) << 12) | (counts[channel_] & 0x0FFFU));
        }
        rx[0] = static_cast<uint8_t>(out >> 8);
        rx[1] = static_cast<uint8_t>(out);
    }

    void Install() {
        flux::bsp::host::SetSpiResponder(Pin(HfFunctionalGpioPin::SPI3_CS_ADS7952_0),
                                         [this](const uint8_t* tx, uint8_t* rx, uint16_t len) { Respond(tx, rx, len); });
    }

private:
    uint8_t channel_{0};
};

}  // namespace flux::test
