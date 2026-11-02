/**
 * @file test_adc_manager.cpp
 * @brief AdcManager on the host: the pin map's channel table onto the MCU ADC and the ADS7952,
 *        divider scaling, fail-closed bring-up and error recording.
 *
 * @copyright HardFOC
 */

#include "HalTestFixture.h"
#include "HostTest.h"
#include "bsp_bind/board/flux_v1/FluxBoardConfig.h"

using flux::hal::ManagerError;
using flux::test::Board;

namespace {

AdcManager& ADC() { return AdcManager::GetInstance(); }

constexpr HfFunctionalAdcChannel kAdsChannels[12] = {
    HfFunctionalAdcChannel::ADS7952_0_CH0, HfFunctionalAdcChannel::ADS7952_0_CH1,
    HfFunctionalAdcChannel::ADS7952_0_CH2, HfFunctionalAdcChannel::ADS7952_0_CH3,
    HfFunctionalAdcChannel::ADS7952_0_CH4, HfFunctionalAdcChannel::ADS7952_0_CH5,
    HfFunctionalAdcChannel::ADS7952_0_CH6, HfFunctionalAdcChannel::ADS7952_0_CH7,
    HfFunctionalAdcChannel::ADS7952_0_CH8, HfFunctionalAdcChannel::ADS7952_0_CH9,
    HfFunctionalAdcChannel::ADS7952_0_CH10, HfFunctionalAdcChannel::ADS7952_0_CH11,
};

}  // namespace

HF_TEST(AdcManager_RegistersEveryPinMapChannel) {
    Board b;
    REQUIRE(ADC().EnsureInitialized());
    CHECK_EQ(ADC().GetManagerError(), ManagerError::None);
    CHECK_EQ(ADC().Size(), HF_ADC_MAPPING_SIZE);
    CHECK(ADC().IsInternalAdcReady());
    CHECK(ADC().IsAds7952Ready(0));
    CHECK(!ADC().IsAds7952Ready(1));

    // The MCU ADC is created with exactly the channels the pin map uses on ADC1 (NTC, VBUS).
    uint32_t expected_mask = 0;
    for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
        const auto& m = HF_ADC_MAPPING[i];
        if (static_cast<HfAdcChipType>(m.chip_type) == HfAdcChipType::ESP32_INTERNAL) {
            expected_mask |= (1UL << m.physical_channel);
        }
    }
    CHECK_EQ(flux::bsp::host::InternalAdcChannelMask(), expected_mask);

    // The ADS7952 sits on its own bus with the board's clock and mode.
    const auto* dev = flux::bsp::host::SpiDeviceSpecOf(flux::test::Pin(HfFunctionalGpioPin::SPI3_CS_ADS7952_0));
    REQUIRE(dev != nullptr);
    CHECK_EQ(dev->clock_hz, flux::board::kAds7952SpiHz);
    CHECK_EQ(dev->mode, flux::board::kAds7952SpiMode);
}

HF_TEST(AdcManager_EachAdsChannelReadsItsOwnInput) {
    Board b;
    for (uint16_t ch = 0; ch < 12; ++ch) {
        b.ads.counts[ch] = static_cast<uint16_t>(100 + 300 * ch);  // distinct per input
    }
    REQUIRE(ADC().EnsureInitialized());
    for (uint8_t ch = 0; ch < 12; ++ch) {
        uint32_t raw = 0;
        REQUIRE(ADC().ReadRaw(kAdsChannels[ch], raw) == hf_adc_err_t::ADC_SUCCESS);
        CHECK_EQ(raw, static_cast<uint32_t>(100 + 300 * ch));
        // The BaseAdc behind every ADS channel is the one Ads7952Handler.
        CHECK(ADC().Get(kAdsChannels[ch]) == ADC().Get(kAdsChannels[0]));
    }
    CHECK(ADC().Get(HfFunctionalAdcChannel::ESP32_VBUS_MONITOR) != ADC().Get(kAdsChannels[0]));
}

HF_TEST(AdcManager_AppliesThePinMapDivider) {
    Board b;
    REQUIRE(ADC().EnsureInitialized());
    const auto* vbus = GetAdcMapping(HfFunctionalAdcChannel::ESP32_VBUS_MONITOR);
    const auto* ntc = GetAdcMapping(HfFunctionalAdcChannel::ESP32_NTC_THERMISTOR);
    REQUIRE(vbus != nullptr);
    REQUIRE(ntc != nullptr);
    auto* mcu = flux::bsp::host::InternalAdc();
    REQUIRE(mcu != nullptr);
    mcu->SetVoltage(vbus->physical_channel, 2.0f);
    mcu->SetVoltage(ntc->physical_channel, 1.2f);

    float v = 0.0f;
    REQUIRE(ADC().ReadVoltage(HfFunctionalAdcChannel::ESP32_VBUS_MONITOR, v) == hf_adc_err_t::ADC_SUCCESS);
    CHECK_NEAR(v, 2.0f * vbus->voltage_divider, 0.05f);  // 11:1 divider on VBUS
    REQUIRE(ADC().ReadVoltage(HfFunctionalAdcChannel::ESP32_NTC_THERMISTOR, v) == hf_adc_err_t::ADC_SUCCESS);
    CHECK_NEAR(v, 1.2f, 0.01f);
}

HF_TEST(AdcManager_NameAndEnumLookupsAgree) {
    Board b;
    REQUIRE(ADC().EnsureInitialized());
    for (size_t i = 0; i < HF_ADC_MAPPING_SIZE; ++i) {
        const auto fc = static_cast<HfFunctionalAdcChannel>(HF_ADC_MAPPING[i].functional_channel);
        CHECK(ADC().Contains(to_string(fc)));
        CHECK(ADC().Get(to_string(fc)) == ADC().Get(fc));
    }
}

HF_TEST(AdcManager_RecordsFailedReads) {
    Board b;
    REQUIRE(ADC().EnsureInitialized());
    float v = 0.0f;
    CHECK(ADC().ReadVoltage("NOT_A_CHANNEL", v) == hf_adc_err_t::ADC_ERR_INVALID_CHANNEL);
    CHECK(ADC().GetLastError() == hf_adc_err_t::ADC_ERR_INVALID_CHANNEL);
    auto* mcu = flux::bsp::host::InternalAdc();
    REQUIRE(mcu != nullptr);
    const auto* vbus = GetAdcMapping(HfFunctionalAdcChannel::ESP32_VBUS_MONITOR);
    mcu->FailChannel(vbus->physical_channel);
    CHECK(ADC().ReadVoltage(HfFunctionalAdcChannel::ESP32_VBUS_MONITOR, v) != hf_adc_err_t::ADC_SUCCESS);
    CHECK(ADC().GetLastError() != hf_adc_err_t::ADC_SUCCESS);
    AdcSystemDiagnostics d{};
    CHECK(ADC().GetSystemDiagnostics(d) == hf_adc_err_t::ADC_SUCCESS);
    CHECK(!d.system_healthy);
    CHECK_EQ(d.failed_operations, 1u);
}

HF_TEST(AdcManager_FailsClosedWhenTheAds7952IsAbsent) {
    Board b;
    b.ads.absent = true;
    CHECK(!ADC().EnsureInitialized());
    CHECK(!ADC().IsInitialized());
    CHECK_EQ(ADC().GetManagerError(), ManagerError::HardwareFault);
    CHECK(!ADC().IsAds7952Ready(0));
    CHECK(ADC().IsInternalAdcReady());  // diagnosis: the MCU ADC did come up
    CHECK(ADC().Get(HfFunctionalAdcChannel::ESP32_VBUS_MONITOR) == nullptr);
    uint32_t raw = 0;
    CHECK(ADC().ReadRaw(HfFunctionalAdcChannel::ADS7952_0_CH0, raw) == hf_adc_err_t::ADC_ERR_NOT_INITIALIZED);
    CHECK(ADC().GetLastError() == hf_adc_err_t::ADC_ERR_NOT_INITIALIZED);
}

HF_TEST(AdcManager_FailsClosedWhenThePressureBusIsDown) {
    Board b;
    flux::bsp::host::FailSpiBus(static_cast<size_t>(flux::board::SpiBus::Pressure));
    CHECK(!ADC().EnsureInitialized());
    CHECK_EQ(ADC().GetManagerError(), ManagerError::BindFailed);
    CHECK(CommChannelsManager::GetInstance().IsInitialized());  // the valve bus alone is comms' required set
    CHECK(!CommChannelsManager::GetInstance().IsPressureBusReady());
}

HF_TEST(AdcManager_FailsClosedWhenTheMcuAdcIsDown) {
    Board b;
    flux::bsp::host::FailInternalAdc();
    CHECK(!ADC().EnsureInitialized());
    CHECK_EQ(ADC().GetManagerError(), ManagerError::BindFailed);
    CHECK(!ADC().IsInternalAdcReady());
    CHECK(ADC().IsAds7952Ready(0));
}

HF_TEST(AdcManager_RetriesAndDeinitializes) {
    Board b;
    b.ads.absent = true;
    CHECK(!ADC().EnsureInitialized());
    b.ads.absent = false;
    REQUIRE(ADC().EnsureInitialized());
    CHECK(ADC().Deinitialize());
    CHECK(!ADC().IsInitialized());
    CHECK_EQ(ADC().GetManagerError(), ManagerError::NotInitialized);
    CHECK(flux::bsp::host::InternalAdc() == nullptr);
    CHECK(!ADC().IsAds7952Ready(0));
}
