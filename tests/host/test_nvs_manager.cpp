/**
 * @file test_nvs_manager.cpp
 * @brief NvsManager on the host: the three namespaces, their isolation and fail-closed opening.
 *
 * @copyright HardFOC
 */

#include <cstring>

#include "HalTestFixture.h"
#include "HostTest.h"

using flux::hal::ManagerError;
using flux::test::ResetHal;

namespace {

NvsManager& NVS() { return NvsManager::GetInstance(); }

struct Clean {
    Clean() { ResetHal(); }
    ~Clean() { ResetHal(); }
};

}  // namespace

HF_TEST(NvsManager_OpensTheThreeFluxNamespaces) {
    Clean c;
    REQUIRE(NVS().EnsureInitialized());
    CHECK_EQ(NVS().GetManagerError(), ManagerError::None);
    CHECK(std::strcmp(NVS().App().GetNamespace(), "flux") == 0);
    CHECK(std::strcmp(NVS().Calibration().GetNamespace(), "flux_cal") == 0);
    CHECK(std::strcmp(NVS().Config().GetNamespace(), "flux_cfg") == 0);
    CHECK(NVS().App().IsInitialized());
    CHECK(NVS().Calibration().IsInitialized());
    CHECK(NVS().Config().IsInitialized());
    // The managers' regions are the binding's slots 0..2.
    CHECK(&NVS().App() == static_cast<BaseNvs*>(flux::bsp::host::Nvs(0)));
    CHECK(&NVS().Calibration() == static_cast<BaseNvs*>(flux::bsp::host::Nvs(1)));
    CHECK(&NVS().Config() == static_cast<BaseNvs*>(flux::bsp::host::Nvs(2)));
}

HF_TEST(NvsManager_NamespacesAreIsolated) {
    Clean c;
    REQUIRE(NVS().EnsureInitialized());
    REQUIRE(NVS().App().SetU32("boot_count", 7) == hf_nvs_err_t::NVS_SUCCESS);
    REQUIRE(NVS().Config().SetU32("boot_count", 9) == hf_nvs_err_t::NVS_SUCCESS);
    uint32_t v = 0;
    REQUIRE(NVS().App().GetU32("boot_count", v) == hf_nvs_err_t::NVS_SUCCESS);
    CHECK_EQ(v, 7u);
    REQUIRE(NVS().Config().GetU32("boot_count", v) == hf_nvs_err_t::NVS_SUCCESS);
    CHECK_EQ(v, 9u);
    CHECK(NVS().Calibration().GetU32("boot_count", v) != hf_nvs_err_t::NVS_SUCCESS);
}

HF_TEST(NvsManager_KeyLengthIsTheEspIdfLimit) {
    Clean c;
    REQUIRE(NVS().EnsureInitialized());
    CHECK_EQ(NVS().App().GetMaxKeyLength(), 15u);
    CHECK(NVS().App().SetU32("a_key_that_is_too_long", 1) != hf_nvs_err_t::NVS_SUCCESS);
}

HF_TEST(NvsManager_FailsClosedWhenOneNamespaceWillNotOpen) {
    Clean c;
    (void)NVS();  // construct the manager, which creates the three regions
    auto* cal = flux::bsp::host::Nvs(1);
    REQUIRE(cal != nullptr);
    cal->SetInitializeResult(hf_nvs_err_t::NVS_ERR_FAILURE);
    CHECK(!NVS().EnsureInitialized());
    CHECK(!NVS().IsInitialized());
    CHECK_EQ(NVS().GetManagerError(), ManagerError::BindFailed);
    CHECK(!NVS().App().IsInitialized());  // the namespace opened before the failure is closed again
}

HF_TEST(NvsManager_DeinitializeClosesEveryNamespace) {
    Clean c;
    REQUIRE(NVS().EnsureInitialized());
    CHECK(NVS().Deinitialize());
    CHECK(!NVS().IsInitialized());
    CHECK_EQ(NVS().GetManagerError(), ManagerError::NotInitialized);
    CHECK(!NVS().App().IsInitialized());
    CHECK(!NVS().Config().IsInitialized());
}
