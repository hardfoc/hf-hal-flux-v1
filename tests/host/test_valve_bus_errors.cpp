/**
 * @file test_valve_bus_errors.cpp
 * @brief Bus errors of the valve chips' control pins reach ValveManager (hf-core bus-error adapters,
 *        hf-max22200-driver GpioSet() returning bool): a MAX22200 whose ENABLE or CMD pin cannot be
 *        driven fails bring-up. With ENABLE stuck low the real chip stays in shutdown; the model
 *        answers anyway, so before the fix bring-up reported the bank ready.
 */
#include "GpioManager.h"
#include "HalTestFixture.h"
#include "HostTest.h"
#include "flux_hal/flux_hal_valve.hpp"

using flux::hal::ManagerError;
using flux::test::Board;
using flux::test::GpioOf;

namespace {
ValveManager& VM() { return ValveManager::GetInstance(); }
}  // namespace

HF_TEST(ValveBusErrors_AStuckMax22200CmdPinFailsBringUp) {
    Board b;
    REQUIRE(GpioManager::GetInstance().EnsureInitialized());
    auto* cmd = GpioOf(HfFunctionalGpioPin::MAX22200_CMD);
    REQUIRE(cmd != nullptr);
    cmd->FailWrites(true);  // e.g. a dead expander: CMD stays where it is

    CHECK(!VM().EnsureInitialized());
    CHECK(!VM().IsBankReady(ValveDriverType::MAX22200));
    CHECK_EQ(VM().GetManagerError(), ManagerError::HardwareFault);
    CHECK_EQ(b.max.ChannelsOn(), 0u);
    CHECK(VM().EnableChannel(ValveChannel::SOL_CH0) != hf_valve_err_t::VALVE_SUCCESS);
    CHECK_EQ(b.max.ChannelsOn(), 0u);
    cmd->FailWrites(false);
}

HF_TEST(ValveBusErrors_AStuckMax22200EnablePinFailsBringUp) {
    Board b;
    REQUIRE(GpioManager::GetInstance().EnsureInitialized());
    auto* enable = GpioOf(HfFunctionalGpioPin::MAX22200_ENABLE);
    REQUIRE(enable != nullptr);
    enable->FailWrites(true);

    CHECK(!VM().EnsureInitialized());
    CHECK(!VM().IsBankReady(ValveDriverType::MAX22200));
    CHECK_EQ(VM().GetManagerError(), ManagerError::HardwareFault);
    CHECK_EQ(b.max.ChannelsOn(), 0u);
    enable->FailWrites(false);
}

HF_TEST(ValveBusErrors_AMax22200CmdPinThatStopsWorkingFailsTheCall) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    auto* cmd = GpioOf(HfFunctionalGpioPin::MAX22200_CMD);
    REQUIRE(cmd != nullptr);
    cmd->FailWrites(true);  // CMD stuck low: the Command Register byte would be taken as data

    CHECK(VM().EnableChannel(ValveChannel::SOL_CH0) != hf_valve_err_t::VALVE_SUCCESS);
    CHECK_EQ(b.max.ChannelsOn(), 0u);
    cmd->FailWrites(false);
}
