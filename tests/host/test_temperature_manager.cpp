/**
 * @file test_temperature_manager.cpp
 * @brief TemperatureManager on the host: the three sensors, the degraded mode and its reporting.
 *
 * @copyright HardFOC
 */

#include "HalTestFixture.h"
#include "HostTest.h"

using flux::hal::ManagerError;
using flux::test::Board;

namespace {

TemperatureManager& TM() { return TemperatureManager::GetInstance(); }

}  // namespace

HF_TEST(TemperatureManager_BringsUpAllThreeSensors) {
    Board b;
    // MCP9700: 500 mV at 0 degC, 10 mV/degC -> 0.75 V at 25 degC. The ADS7952 runs the 2 x Vref
    // range (5.0 V full scale over 12 bits) in the handler's default configuration.
    b.ads.counts[8] = static_cast<uint16_t>(0.75f / 5.0f * 4095.0f);
    REQUIRE(TM().EnsureInitialized());
    CHECK_EQ(TM().GetManagerError(), ManagerError::None);
    CHECK(!TM().IsDegraded());
    CHECK_EQ(TM().GetSensorCount(), 3u);
    CHECK(TM().IsSensorReady(FluxTempSensorType::ESP32_INTERNAL));
    CHECK(TM().IsSensorReady(FluxTempSensorType::NTC_THERMISTOR));
    CHECK(TM().IsSensorReady(FluxTempSensorType::MCP9700_THERMISTOR));

    auto* die = flux::bsp::host::DieTemperature();
    REQUIRE(die != nullptr);
    die->SetCelsius(41.5f);
    float c = 0.0f;
    REQUIRE(TM().ReadInternalTemperature(c) == TemperatureError::SUCCESS);
    CHECK_NEAR(c, 41.5f, 0.01f);

    REQUIRE(TM().ReadMcp9700Temperature(c) == TemperatureError::SUCCESS);
    CHECK_NEAR(c, 25.0f, 1.0f);
}

HF_TEST(TemperatureManager_NtcReadsThroughItsPinMapChannel) {
    Board b;
    REQUIRE(TM().EnsureInitialized());
    auto* mcu = flux::bsp::host::InternalAdc();
    REQUIRE(mcu != nullptr);
    const auto* ntc = GetAdcMapping(HfFunctionalAdcChannel::ESP32_NTC_THERMISTOR);
    REQUIRE(ntc != nullptr);
    mcu->SetVoltage(ntc->physical_channel, 1.65f);  // 10 k NTC against 10 k at 3.3 V: 25 degC
    float c = 0.0f;
    REQUIRE(TM().ReadNtcTemperature(c) == TemperatureError::SUCCESS);
    CHECK_NEAR(c, 25.0f, 2.0f);
    CHECK_EQ(mcu->ReadCount(ntc->physical_channel) > 0, true);
}

HF_TEST(TemperatureManager_RunsDegradedWithoutTheAdc) {
    Board b;
    b.ads.absent = true;  // ADC manager fails closed, so both thermistors are missing
    REQUIRE(TM().EnsureInitialized());
    CHECK(TM().IsDegraded());
    CHECK_EQ(TM().GetManagerError(), ManagerError::DependencyNotReady);
    CHECK(TM().IsSensorReady(FluxTempSensorType::ESP32_INTERNAL));
    CHECK(!TM().IsSensorReady(FluxTempSensorType::NTC_THERMISTOR));
    float c = 0.0f;
    CHECK(TM().ReadInternalTemperature(c) == TemperatureError::SUCCESS);  // the fallback works
    CHECK(TM().ReadNtcTemperature(c) == TemperatureError::SENSOR_NOT_FOUND);
    CHECK(TM().GetLastError() == TemperatureError::SENSOR_NOT_FOUND);
    TempSystemDiagnostics d{};
    CHECK(TM().GetSystemDiagnostics(d) == TemperatureError::SUCCESS);
    CHECK(!d.system_healthy);
}

HF_TEST(TemperatureManager_RunsDegradedWithoutTheDieSensor) {
    Board b;
    flux::bsp::host::FailDieTemperatureInit();
    REQUIRE(TM().EnsureInitialized());
    CHECK(TM().IsDegraded());
    CHECK_EQ(TM().GetManagerError(), ManagerError::HardwareFault);
    CHECK(!TM().IsSensorReady(FluxTempSensorType::ESP32_INTERNAL));
    CHECK(TM().IsSensorReady(FluxTempSensorType::NTC_THERMISTOR));
}

HF_TEST(TemperatureManager_FailsWithNoSensorAtAll) {
    Board b;
    b.ads.absent = true;
    flux::bsp::host::FailDieTemperatureInit();
    CHECK(!TM().EnsureInitialized());
    CHECK(!TM().IsInitialized());
    CHECK(TM().GetLastError() == TemperatureError::INITIALIZATION_FAILED);
}

HF_TEST(TemperatureManager_DeinitializeReleasesEverySensor) {
    Board b;
    REQUIRE(TM().EnsureInitialized());
    CHECK(TM().Deinitialize());
    CHECK(!TM().IsInitialized());
    CHECK_EQ(TM().GetSensorCount(), 0u);
    CHECK(flux::bsp::host::DieTemperature() == nullptr);
    CHECK(!TM().IsSensorReady(FluxTempSensorType::NTC_THERMISTOR));
}
