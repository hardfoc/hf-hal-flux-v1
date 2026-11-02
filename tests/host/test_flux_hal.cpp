/**
 * @file test_flux_hal.cpp
 * @brief The Flux facade on the host: init order, required vs optional managers, aggregated
 *        diagnostics in the shared ManagerError catalog.
 *
 * @copyright HardFOC
 */

#include <cstring>

#include "HalTestFixture.h"
#include "HostTest.h"
#include "bsp_bind/board/flux_v1/FluxBoardConfig.h"

using flux::hal::ManagerError;
using flux::test::Board;

HF_TEST(Flux_HealthyBoardReportsEveryPartReady) {
    Board b;
    auto& flux = Flux::GetInstance();
    REQUIRE(flux.EnsureInitialized());
    CHECK_EQ(flux.GetLastError(), ManagerError::None);

    FluxHalDiagnostics d{};
    CHECK(flux.CollectDiagnostics(d));
    CHECK(d.initialized);
    CHECK(d.all_managers_up);
    CHECK(d.all_healthy);
    CHECK_EQ(d.first_error, ManagerError::None);
    CHECK(d.max22200_ready);
    CHECK(d.tle92466_ready);
    CHECK(d.pressure_bus_ready);
    CHECK(d.ads7952_ready);
    CHECK(d.internal_adc_ready);
    CHECK(d.can_ready);
    CHECK(d.die_temp_ready);
    CHECK(d.ntc_ready);
    CHECK(d.mcp9700_ready);
    CHECK(d.status_led_ready);

    // Init order and the required set, as documented.
    const char* order[] = {"NvsManager", "CommChannelsManager", "GpioManager", "ValveManager",
                           "AdcManager", "LedManager", "TemperatureManager"};
    for (size_t i = 0; i < kFluxManagerCount; ++i) {
        CHECK(std::strcmp(d.managers[i].name, order[i]) == 0);
        CHECK_EQ(d.managers[i].required, i < 3);
        CHECK(d.managers[i].initialized);
        CHECK_EQ(d.managers[i].error, ManagerError::None);
    }
}

HF_TEST(Flux_MissingValveDriverIsOptionalButNamed) {
    Board b;
    b.tle.absent = true;
    auto& flux = Flux::GetInstance();
    CHECK(flux.EnsureInitialized());  // valves are optional at the facade
    CHECK_EQ(flux.GetLastError(), ManagerError::None);

    FluxHalDiagnostics d{};
    (void)flux.CollectDiagnostics(d);
    CHECK(d.initialized);
    CHECK(!d.all_managers_up);
    CHECK(!d.all_healthy);
    CHECK_EQ(d.first_error, ManagerError::HardwareFault);
    REQUIRE(d.first_error_manager != nullptr);
    CHECK(std::strcmp(d.first_error_manager, "ValveManager") == 0);
    CHECK(d.max22200_ready);
    CHECK(!d.tle92466_ready);
    CHECK(!ValveManager::GetInstance().IsInitialized());
}

HF_TEST(Flux_MissingValveBusFailsTheRequiredSet) {
    Board b;
    flux::bsp::host::FailSpiBus(static_cast<size_t>(flux::board::SpiBus::Valve));
    auto& flux = Flux::GetInstance();
    CHECK(!flux.EnsureInitialized());
    CHECK(!flux.IsInitialized());
    CHECK_EQ(flux.GetLastError(), ManagerError::BindFailed);

    FluxHalDiagnostics d{};
    (void)flux.CollectDiagnostics(d);
    CHECK(!d.initialized);
    REQUIRE(d.first_error_manager != nullptr);
    CHECK(std::strcmp(d.first_error_manager, "CommChannelsManager") == 0);
    CHECK(d.managers[0].initialized);   // NVS came up first
    CHECK(!d.managers[1].initialized);  // comms failed
    CHECK(!d.managers[3].initialized);  // valves were never attempted
}

HF_TEST(Flux_DegradedTemperatureIsReportedNotHidden) {
    Board b;
    flux::bsp::host::FailDieTemperatureInit();
    auto& flux = Flux::GetInstance();
    REQUIRE(flux.EnsureInitialized());
    FluxHalDiagnostics d{};
    (void)flux.CollectDiagnostics(d);
    CHECK(d.all_managers_up);  // the temperature manager runs ...
    CHECK(!d.all_healthy);     // ... degraded, and says so
    CHECK(!d.die_temp_ready);
    CHECK(std::strcmp(d.first_error_manager, "TemperatureManager") == 0);
}

HF_TEST(Flux_SystemCallsGoThroughTheBinding) {
    Board b;
    flux::bsp::host::SetResetReason(4);
    CHECK_EQ(Flux::GetInstance().GetLastResetReason(), 4u);
}
