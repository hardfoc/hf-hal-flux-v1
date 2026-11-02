/**
 * @file test_valve_manager.cpp
 * @brief ValveManager on the host: channel map onto the MAX22200 and TLE92466ED, control pins,
 *        fail-closed bring-up, fault reporting and shutdown, against register-level chip models.
 *
 * @copyright HardFOC
 */

#include <array>
#include <set>
#include <string_view>

#include "HalTestFixture.h"
#include "HostTest.h"
#include "bsp_bind/board/flux_v1/FluxBoardConfig.h"

using flux::hal::ManagerError;
using flux::test::Board;
using flux::test::GpioOf;

namespace {

ValveManager& VM() { return ValveManager::GetInstance(); }

uint16_t TleChCtrl(const Board& b) { return b.tle.Reg(tle92466ed::CentralReg::CH_CTRL); }

uint16_t TleSetpoint(const Board& b, uint8_t ch) {
    return b.tle.Reg(static_cast<uint16_t>(tle92466ed::GetChannelBase(static_cast<tle92466ed::Channel>(ch)) +
                                           tle92466ed::ChannelReg::SETPOINT));
}

}  // namespace

//==============================================================================
// Channel map (the table itself)
//==============================================================================

HF_TEST(ValveChannelMap_IsBijectiveOntoTheTwoChips) {
    CHECK_EQ(kValveChannelCount, static_cast<size_t>(ValveChannel::CHANNEL_COUNT));
    std::set<std::string_view> names;
    std::set<uint8_t> max_hw;
    std::set<uint8_t> tle_hw;
    for (size_t i = 0; i < kValveChannelCount; ++i) {
        const auto& row = kValveChannelMap[i];
        CHECK_EQ(static_cast<size_t>(row.channel), i);  // enum value == row index (O(1) lookup)
        CHECK(names.insert(row.name).second);
        if (i < 8) {
            CHECK(row.driver_type == ValveDriverType::MAX22200);
            CHECK_EQ(row.hw_channel, static_cast<uint8_t>(i));  // SOL_CHn -> MAX22200 OUTn
            CHECK(max_hw.insert(row.hw_channel).second);
        } else {
            CHECK(row.driver_type == ValveDriverType::TLE92466);
            CHECK_EQ(row.hw_channel, static_cast<uint8_t>(i - 8));  // PROP_CHn -> TLE92466ED OUTn
            CHECK(tle_hw.insert(row.hw_channel).second);
        }
    }
    CHECK_EQ(max_hw.size(), 8u);
    CHECK_EQ(tle_hw.size(), 6u);
}

HF_TEST(ValveChannelMap_NameLookupRoundTrips) {
    Board b;
    for (const auto& row : kValveChannelMap) {
        CHECK(VM().FindChannel(row.name) == row.channel);
        const auto* info = VM().GetChannelInfo(row.name);
        REQUIRE(info != nullptr);
        CHECK(info->channel == row.channel);
    }
    CHECK(VM().FindChannel("NOPE") == ValveChannel::CHANNEL_COUNT);
    CHECK(VM().GetChannelInfo("NOPE") == nullptr);
}

//==============================================================================
// Bring-up
//==============================================================================

HF_TEST(ValveManager_BringsUpBothBanksAndConfiguresThePins) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    CHECK(VM().IsInitialized());
    CHECK(VM().IsBankReady(ValveDriverType::MAX22200));
    CHECK(VM().IsBankReady(ValveDriverType::TLE92466));
    CHECK_EQ(VM().GetManagerError(), ManagerError::None);
    CHECK(VM().GetMax22200Handler() != nullptr);
    CHECK(VM().GetTle92466Handler() != nullptr);

    // MAX22200 left ACTIVE with every channel off; TLE92466ED in mission mode, every channel off.
    CHECK((b.max.regs[max22200::RegBank::STATUS] & max22200::StatusReg::ACTIVE_BIT) != 0u);
    CHECK_EQ(b.max.ChannelsOn(), 0u);
    CHECK((TleChCtrl(b) & tle92466ed::CH_CTRL::OP_MODE) != 0u);
    CHECK_EQ(TleChCtrl(b) & tle92466ed::CH_CTRL::ALL_CH_MASK, 0u);

    // Fault outputs of both chips are inputs; today's bring-up leaves both enables driven high.
    auto* max_fault = GpioOf(HfFunctionalGpioPin::MAX22200_FAULT_N);
    auto* tle_fault = GpioOf(HfFunctionalGpioPin::TLE92466_FAULT_N);
    REQUIRE(max_fault != nullptr);
    REQUIRE(tle_fault != nullptr);
    CHECK(max_fault->IsInput());
    CHECK(tle_fault->IsInput());
    CHECK(GpioOf(HfFunctionalGpioPin::MAX22200_ENABLE)->DrivenLevel() == hf_gpio_level_t::HF_GPIO_LEVEL_HIGH);
    CHECK(GpioOf(HfFunctionalGpioPin::TLE92466_EN)->DrivenLevel() == hf_gpio_level_t::HF_GPIO_LEVEL_HIGH);
}

HF_TEST(ValveManager_UsesTheBoardSpiSetup) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    const auto* max_dev = flux::bsp::host::SpiDeviceSpecOf(flux::test::Pin(HfFunctionalGpioPin::SPI2_CS_MAX22200));
    const auto* tle_dev = flux::bsp::host::SpiDeviceSpecOf(flux::test::Pin(HfFunctionalGpioPin::SPI2_CS_TLE92466));
    REQUIRE(max_dev != nullptr);
    REQUIRE(tle_dev != nullptr);
    CHECK_EQ(max_dev->mode, flux::board::kMax22200SpiMode);
    CHECK_EQ(tle_dev->mode, flux::board::kTle92466SpiMode);
    CHECK_EQ(max_dev->clock_hz, flux::board::kMax22200SpiHz);
    CHECK_EQ(tle_dev->clock_hz, flux::board::kTle92466SpiHz);
    const auto* bus = flux::bsp::host::SpiBusSpecOf(static_cast<size_t>(flux::board::SpiBus::Valve));
    REQUIRE(bus != nullptr);
    CHECK_EQ(bus->controller, flux::board::kValveSpiController);
    CHECK_EQ(bus->sclk, flux::test::Pin(HfFunctionalGpioPin::SPI2_SCK));
}

//==============================================================================
// Channel mapping on the wire
//==============================================================================

HF_TEST(ValveManager_EachSolenoidChannelDrivesItsOwnMaxOutput) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    for (uint8_t n = 0; n < 8; ++n) {
        const auto ch = static_cast<ValveChannel>(n);
        REQUIRE(VM().EnableChannel(ch) == hf_valve_err_t::VALVE_SUCCESS);
        CHECK_EQ(b.max.ChannelsOn(), static_cast<uint8_t>(1u << n));
        CHECK(VM().IsChannelEnabled(ch));
        CHECK_EQ(TleChCtrl(b) & tle92466ed::CH_CTRL::ALL_CH_MASK, 0u);  // never touches the other bank
        REQUIRE(VM().DisableChannel(ch) == hf_valve_err_t::VALVE_SUCCESS);
        CHECK_EQ(b.max.ChannelsOn(), 0u);
        CHECK(!VM().IsChannelEnabled(ch));
    }
}

HF_TEST(ValveManager_EachProportionalChannelDrivesItsOwnTleOutput) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    for (uint8_t n = 0; n < 6; ++n) {
        const auto ch = static_cast<ValveChannel>(8 + n);
        REQUIRE(VM().EnableChannel(ch) == hf_valve_err_t::VALVE_SUCCESS);
        CHECK_EQ(TleChCtrl(b) & tle92466ed::CH_CTRL::ALL_CH_MASK, tle92466ed::CH_CTRL::ChannelMask(n));
        CHECK_EQ(b.max.ChannelsOn(), 0u);
        REQUIRE(VM().DisableChannel(ch) == hf_valve_err_t::VALVE_SUCCESS);
        CHECK_EQ(TleChCtrl(b) & tle92466ed::CH_CTRL::ALL_CH_MASK, 0u);
    }
}

HF_TEST(ValveManager_ProportionalCurrentLandsOnTheMappedChannelOnly) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    std::array<uint16_t, 6> before{};
    for (uint8_t n = 0; n < 6; ++n) before[n] = TleSetpoint(b, n);

    REQUIRE(VM().SetProportionalCurrent(ValveChannel::PROP_CH2, 500) == hf_valve_err_t::VALVE_SUCCESS);
    CHECK(TleSetpoint(b, 2) != before[2]);
    for (uint8_t n = 0; n < 6; ++n) {
        if (n != 2) CHECK_EQ(TleSetpoint(b, n), before[n]);
    }
    const auto* st = VM().GetChannelState(ValveChannel::PROP_CH2);
    REQUIRE(st != nullptr);
    CHECK_EQ(st->current_ma, 500u);
}

HF_TEST(ValveManager_ChannelMaskSplitsAcrossTheBanks) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    // Bits 0-7: MAX22200 OUT0-7; bits 8-13: TLE92466ED OUT0-5.
    const uint16_t mask = static_cast<uint16_t>((1u << 0) | (1u << 5) | (1u << (8 + 1)) | (1u << (8 + 4)));
    REQUIRE(VM().EnableChannelsMask(mask) == hf_valve_err_t::VALVE_SUCCESS);
    CHECK_EQ(b.max.ChannelsOn(), static_cast<uint8_t>((1u << 0) | (1u << 5)));
    CHECK_EQ(TleChCtrl(b) & tle92466ed::CH_CTRL::ALL_CH_MASK,
             static_cast<uint16_t>(tle92466ed::CH_CTRL::EN_CH1 | tle92466ed::CH_CTRL::EN_CH4));
    REQUIRE(VM().DisableAllChannels() == hf_valve_err_t::VALVE_SUCCESS);
    CHECK_EQ(b.max.ChannelsOn(), 0u);
    CHECK_EQ(TleChCtrl(b) & tle92466ed::CH_CTRL::ALL_CH_MASK, 0u);
}

HF_TEST(ValveManager_RefusesTheWrongKindOfChannelAndRecordsIt) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    CHECK(VM().ConfigureSolenoidCdr(ValveChannel::PROP_CH0, 500, 200, 10.0f) ==
          hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);
    CHECK(VM().GetLastError() == hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);
    CHECK(VM().SetProportionalCurrent(ValveChannel::SOL_CH0, 100) == hf_valve_err_t::VALVE_ERR_CHANNEL_TYPE_MISMATCH);
    CHECK(VM().ConfigureProportionalPwm(ValveChannel::PROP_CH0, 0.0f) == hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER);
    CHECK(VM().GetLastError() == hf_valve_err_t::VALVE_ERR_INVALID_PARAMETER);
    CHECK(VM().EnableChannel(ValveChannel::CHANNEL_COUNT) == hf_valve_err_t::VALVE_ERR_INVALID_CHANNEL);
}

//==============================================================================
// Fail-closed bring-up
//==============================================================================

HF_TEST(ValveManager_FailsClosedWhenTheTleIsAbsent) {
    Board b;
    b.tle.absent = true;
    CHECK(!VM().EnsureInitialized());
    CHECK(!VM().IsInitialized());
    CHECK_EQ(VM().GetManagerError(), ManagerError::HardwareFault);
    CHECK(VM().IsBankReady(ValveDriverType::MAX22200));   // diagnosis: which part answered
    CHECK(!VM().IsBankReady(ValveDriverType::TLE92466));
    CHECK(VM().GetMax22200Handler() != nullptr);
    CHECK(VM().GetTle92466Handler() == nullptr);

    // Every channel operation is refused, on both banks, and recorded.
    CHECK(VM().EnableChannel(ValveChannel::SOL_CH0) == hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    CHECK(VM().EnableChannel(ValveChannel::PROP_CH0) == hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    CHECK(VM().GetLastError() == hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
    CHECK_EQ(b.max.ChannelsOn(), 0u);

    ValveSystemDiagnostics d{};
    CHECK(VM().GetSystemDiagnostics(d) == hf_valve_err_t::VALVE_SUCCESS);
    CHECK(!d.system_healthy);
}

HF_TEST(ValveManager_FailsClosedWhenTheMaxIsAbsent) {
    Board b;
    b.max.absent = true;
    CHECK(!VM().EnsureInitialized());
    CHECK_EQ(VM().GetManagerError(), ManagerError::HardwareFault);
    CHECK(!VM().IsBankReady(ValveDriverType::MAX22200));
    CHECK(VM().IsBankReady(ValveDriverType::TLE92466));
    CHECK(VM().SetProportionalCurrent(ValveChannel::PROP_CH1, 100) == hf_valve_err_t::VALVE_ERR_NOT_INITIALIZED);
}

HF_TEST(ValveManager_ReportsAMissingBusAsADependency) {
    Board b;
    flux::bsp::host::FailSpiBus(static_cast<size_t>(flux::board::SpiBus::Valve));
    CHECK(!VM().EnsureInitialized());
    CHECK_EQ(VM().GetManagerError(), ManagerError::DependencyNotReady);
    CHECK_EQ(CommChannelsManager::GetInstance().GetManagerError(), ManagerError::BindFailed);
    CHECK(!VM().IsBankReady(ValveDriverType::MAX22200));
    CHECK(!VM().IsBankReady(ValveDriverType::TLE92466));
}

HF_TEST(ValveManager_RetriesFromScratchAfterAFailedBringUp) {
    Board b;
    b.tle.absent = true;
    CHECK(!VM().EnsureInitialized());
    b.tle.absent = false;
    CHECK(VM().EnsureInitialized());
    CHECK_EQ(VM().GetManagerError(), ManagerError::None);
    CHECK(VM().IsBankReady(ValveDriverType::TLE92466));
}

//==============================================================================
// Faults
//==============================================================================

HF_TEST(ValveManager_ReportsMaxFaultFlagsAndChannelFaults) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    CHECK(!VM().HasSolenoidFault());

    b.max.fault_flags = static_cast<uint8_t>(max22200::StatusReg::OVT_BIT);
    CHECK(VM().HasSolenoidFault());
    CHECK(VM().HasFault());

    b.max.regs[max22200::RegBank::FAULT] = (1u << (max22200::FaultReg::OCP_SHIFT + 3));  // OCP on OUT3
    max22200::FaultStatus fs{};
    REQUIRE(VM().GetSolenoidFaults(fs) == hf_valve_err_t::VALVE_SUCCESS);
    CHECK_EQ(fs.overcurrent_channel_mask, static_cast<uint8_t>(1u << 3));

    b.max.fault_flags = 0;
    CHECK(!VM().HasSolenoidFault());
}

HF_TEST(ValveManager_ReportsTleSupplyFaults) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    CHECK(!VM().HasProportionalFault());
    b.tle.regs[tle92466ed::CentralReg::GLOBAL_DIAG0] = tle92466ed::GLOBAL_DIAG0::VBAT_UV;
    CHECK(VM().HasProportionalFault());
    tle92466ed::FaultReport report{};
    REQUIRE(VM().GetProportionalFaults(report) == hf_valve_err_t::VALVE_SUCCESS);
    CHECK(report.vbat_uv);
}

HF_TEST(ValveManager_FaultQueriesOnAMissingBankAreRefused) {
    Board b;
    b.tle.absent = true;
    (void)VM().EnsureInitialized();
    tle92466ed::FaultReport report{};
    CHECK(VM().GetProportionalFaults(report) == hf_valve_err_t::VALVE_ERR_DRIVER_NOT_AVAILABLE);
    CHECK(!VM().HasProportionalFault());
}

//==============================================================================
// Shutdown
//==============================================================================

HF_TEST(ValveManager_DeinitializeTurnsEveryChannelOffFirst) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    REQUIRE(VM().EnableChannel(ValveChannel::SOL_CH6) == hf_valve_err_t::VALVE_SUCCESS);
    REQUIRE(VM().EnableChannel(ValveChannel::PROP_CH3) == hf_valve_err_t::VALVE_SUCCESS);
    REQUIRE(b.max.ChannelsOn() != 0u);
    CHECK(VM().Deinitialize());
    CHECK_EQ(b.max.ChannelsOn(), 0u);
    CHECK_EQ(TleChCtrl(b) & tle92466ed::CH_CTRL::ALL_CH_MASK, 0u);
    CHECK(!VM().IsInitialized());
    CHECK_EQ(VM().GetManagerError(), ManagerError::NotInitialized);
    CHECK(VM().GetMax22200Handler() == nullptr);
}

HF_TEST(Flux_ShutdownIncludesTheValves) {
    Board b;
    auto& flux = Flux::GetInstance();
    REQUIRE(flux.EnsureInitialized());
    REQUIRE(VM().IsInitialized());
    REQUIRE(VM().EnableChannel(ValveChannel::SOL_CH2) == hf_valve_err_t::VALVE_SUCCESS);
    CHECK(flux.Shutdown());
    CHECK(!VM().IsInitialized());
    CHECK_EQ(b.max.ChannelsOn(), 0u);
}
