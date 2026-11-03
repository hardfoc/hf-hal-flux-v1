/**
 * @file test_valve_driver_access.cpp
 * @brief ValveManager hands out no raw chip driver: no handler pointer (whose GetDriver() skips the
 *        handler mutex), only access objects whose calls each take the handler's lock, and whose
 *        WithDriver() runs driver code under that lock.
 */
#include <type_traits>
#include <utility>

#include "HalTestFixture.h"
#include "HostTest.h"
#include "flux_hal/flux_hal_valve.hpp"

using flux::test::Board;

namespace {

ValveManager& VM() { return ValveManager::GetInstance(); }

template <typename T, typename = void>
struct ExposesMaxHandler : std::false_type {};
template <typename T>
struct ExposesMaxHandler<T, std::void_t<decltype(std::declval<T&>().GetMax22200Handler())>>
    : std::true_type {};

template <typename T, typename = void>
struct ExposesTleHandler : std::false_type {};
template <typename T>
struct ExposesTleHandler<T, std::void_t<decltype(std::declval<T&>().GetTle92466Handler())>>
    : std::true_type {};

template <typename T, typename = void>
struct HasGetDriver : std::false_type {};
template <typename T>
struct HasGetDriver<T, std::void_t<decltype(std::declval<T&>().GetDriver())>> : std::true_type {};

static_assert(!ExposesMaxHandler<ValveManager>::value,
              "ValveManager must not hand out the MAX22200 handler (its GetDriver() is unlocked)");
static_assert(!ExposesTleHandler<ValveManager>::value,
              "ValveManager must not hand out the TLE92466ED handler (its GetDriver() is unlocked)");
static_assert(!HasGetDriver<ValveManager::Max22200Access>::value,
              "the MAX22200 access object must not expose the raw driver");
static_assert(!HasGetDriver<ValveManager::Tle92466Access>::value,
              "the TLE92466ED access object must not expose the raw driver");

}  // namespace

HF_TEST(ValveAccess_IsAvailableOnlyWhileTheBankIsUp) {
    Board b;
    CHECK(!VM().Max22200());  // before bring-up
    CHECK(!VM().Tle92466());
    REQUIRE(VM().EnsureInitialized());
    CHECK(static_cast<bool>(VM().Max22200()));
    CHECK(static_cast<bool>(VM().Tle92466()));
    CHECK(VM().Deinitialize());
    CHECK(!VM().Max22200());
    CHECK(!VM().Tle92466());
}

HF_TEST(ValveAccess_ForwardsTheLockedHandlerApi) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    max22200::StatusConfig st{};
    CHECK(VM().Max22200().GetStatus(st) == max22200::DriverStatus::OK);
    CHECK(st.active);
    CHECK(VM().Tle92466().GetIcVersion() != 0u);
}

HF_TEST(ValveAccess_WithDriverRunsDriverCallsUnderTheHandler) {
    Board b;
    REQUIRE(VM().EnsureInitialized());
    uint32_t raw = 0;
    const auto rs = VM().Max22200().WithDriver([&](auto& drv) {
        return drv.ReadRegister32(max22200::RegBank::STATUS, raw);
    });
    CHECK(rs == max22200::DriverStatus::OK);
    CHECK((raw & max22200::StatusReg::ACTIVE_BIT) != 0u);
}

HF_TEST(ValveAccess_AnEmptyAccessRefusesEveryCall) {
    Board b;  // not brought up
    max22200::StatusConfig st{};
    CHECK(VM().Max22200().GetStatus(st) == max22200::DriverStatus::INITIALIZATION_ERROR);
    tle92466ed::ChannelDiagnostics diag{};
    CHECK(!VM().Tle92466().GetChannelDiagnostics(0, diag).has_value());
    CHECK(!VM().Tle92466().EnterConfigMode().has_value());
    uint32_t raw = 0;
    const auto rs = VM().Max22200().WithDriver(
        [&](auto& drv) { return drv.ReadRegister32(max22200::RegBank::STATUS, raw); });
    CHECK(rs == max22200::DriverStatus::INITIALIZATION_ERROR);
}
