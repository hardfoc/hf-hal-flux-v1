/**
 * @file flux_nvs_example.cpp
 * @brief Flux HAL — NVS (`NvsManager` / `BaseNvs`) smoke test on ESP32-S3.
 *
 * After `Flux::EnsureInitialized()`, exercises all three namespaces:
 *   - `flux`       (`App()`)         — persistent boot counter
 *   - `flux_cal`   (`Calibration()`) — example u32 + small blob
 *   - `flux_cfg`   (`Config()`)      — short string round-trip
 *
 * Build:  ./scripts/build_app.sh flux_nvs_example Debug
 * Flash:  ./scripts/flash_app.sh flash_monitor flux_nvs_example Debug
 */

#include "api/Flux.h"
#include "managers/NvsManager.h"
#include "core/hf-core-drivers/internal/hf-internal-interface-wrap/inc/base/BaseNvs.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

static const char* TAG = "FluxNvsEx";

namespace {

constexpr const char* kKeyBootCount = "boot_cnt";
constexpr const char* kKeyCalMagic  = "cal_magic";
constexpr const char* kKeyCfgTag    = "cfg_tag";

constexpr hf_u32_t kCalMagicExpected = 0xC0FFEE01U;

void LogErr(const char* what, hf_nvs_err_t e) noexcept {
    ESP_LOGE(TAG, "%s failed: err=%d", what, static_cast<int>(e));
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Flux NVS example — ESP32-S3");

    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        ESP_LOGE(TAG, "Flux::EnsureInitialized() failed — check HAL / hardware");
        return;
    }

    BaseNvs& app = flux.nvs.App();
    BaseNvs& cal = flux.nvs.Calibration();
    BaseNvs& cfg = flux.nvs.Config();

    ESP_LOGI(TAG, "Namespaces: app='%s' cal='%s' cfg='%s'", app.GetNamespace(), cal.GetNamespace(),
             cfg.GetNamespace());

    // --- App namespace: monotonic boot counter ---
    hf_u32_t boot_count = 0;
    hf_nvs_err_t e = app.GetU32(kKeyBootCount, boot_count);
    if (e != hf_nvs_err_t::NVS_SUCCESS) {
        ESP_LOGW(TAG, "No prior boot_cnt (first boot or missing key), err=%d", static_cast<int>(e));
        boot_count = 0;
    } else {
        ESP_LOGI(TAG, "Previous boot_cnt=%lu", static_cast<unsigned long>(boot_count));
    }
    ++boot_count;
    e = app.SetU32(kKeyBootCount, boot_count);
    if (e != hf_nvs_err_t::NVS_SUCCESS) {
        LogErr("App SetU32(boot_cnt)", e);
        return;
    }
    ESP_LOGI(TAG, "Stored boot_cnt=%lu", static_cast<unsigned long>(boot_count));

    // --- Calibration namespace: u32 + tiny blob ---
    hf_u32_t magic = 0;
    e = cal.GetU32(kKeyCalMagic, magic);
    if (e != hf_nvs_err_t::NVS_SUCCESS) {
        ESP_LOGW(TAG, "cal_magic read: err=%d (seeding)", static_cast<int>(e));
    }
    e = cal.SetU32(kKeyCalMagic, kCalMagicExpected);
    if (e != hf_nvs_err_t::NVS_SUCCESS) {
        LogErr("Cal SetU32(cal_magic)", e);
        return;
    }
    const uint8_t blob_in[] = {0x01, 0x02, 0x03, 0x04};
    e = cal.SetBlob("cal_blob", blob_in, sizeof(blob_in));
    if (e != hf_nvs_err_t::NVS_SUCCESS) {
        LogErr("Cal SetBlob", e);
        return;
    }
    uint8_t blob_out[8]{};
    size_t got = 0;
    e = cal.GetBlob("cal_blob", blob_out, sizeof(blob_out), &got);
    if (e != hf_nvs_err_t::NVS_SUCCESS || got != sizeof(blob_in) ||
        std::memcmp(blob_in, blob_out, sizeof(blob_in)) != 0) {
        ESP_LOGE(TAG, "Cal GetBlob mismatch (err=%d, got=%zu)", static_cast<int>(e), got);
        return;
    }
    ESP_LOGI(TAG, "Calibration u32+blob OK");

    // --- Config namespace: string ---
    const char* tag = "flux_nvs_v1";
    e = cfg.SetString(kKeyCfgTag, tag);
    if (e != hf_nvs_err_t::NVS_SUCCESS) {
        LogErr("Cfg SetString", e);
        return;
    }
    char buf[32]{};
    e = cfg.GetString(kKeyCfgTag, buf, sizeof(buf), nullptr);
    if (e != hf_nvs_err_t::NVS_SUCCESS || std::strcmp(buf, tag) != 0) {
        ESP_LOGE(TAG, "Cfg GetString mismatch (err=%d)", static_cast<int>(e));
        return;
    }
    ESP_LOGI(TAG, "Config string OK: \"%s\"", buf);

    ESP_LOGI(TAG, "All NVS checks passed. Reset the board to see boot_cnt increment.");

    while (true) {
        hf_u32_t bc = 0;
        if (app.GetU32(kKeyBootCount, bc) == hf_nvs_err_t::NVS_SUCCESS) {
            ESP_LOGI(TAG, "alive: boot_cnt=%lu nvs_err=%d", static_cast<unsigned long>(bc),
                     static_cast<int>(flux.nvs.GetLastError()));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
