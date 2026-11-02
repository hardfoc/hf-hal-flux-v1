# NvsManager

Gives the application three separate key-value stores in the ESP32's NVS flash partition, so calibration data,
configuration and ordinary application data never collide. Source: [`NvsManager.h`](../../lib/managers/NvsManager.h).

| Accessor | NVS namespace | Use it for |
| --- | --- | --- |
| `App()` | `flux` | Counters, last state, anything the application owns |
| `Calibration()` | `flux_cal` | Factory or bench calibration |
| `Config()` | `flux_cfg` | User or deployment configuration |

Each accessor returns a `BaseNvs&` (the interface class; on the ESP32-S3 the platform binding backs it with `EspNvs`).

## API

| Call | Notes |
| --- | --- |
| `App()`, `Calibration()`, `Config()` | `BaseNvs&` for the namespace |
| `BaseNvs::SetU32 / GetU32`, `SetString / GetString`, `SetBlob / GetBlob` | Return `hf_nvs_err_t`; the ESP32 implementation commits each write |
| `BaseNvs::KeyExists`, `EraseKey`, `GetSize`, `Commit` | |
| `EnsureInitialized()`, `IsInitialized()`, `Deinitialize()` | Opens / closes all three namespaces |
| `GetLastError()`, `DumpStatistics()` | `DumpStatistics()` logs each namespace and whether it is open |

## Example: a boot counter

```cpp
#include "api/Flux.h"
#include "managers/NvsManager.h"

auto& flux = Flux::GetInstance();
if (flux.EnsureInitialized()) {
    BaseNvs& app = flux.nvs.App();
    hf_u32_t boots = 0;
    if (app.GetU32("boot_cnt", boots) != hf_nvs_err_t::NVS_SUCCESS) {
        boots = 0;  // first boot, or the key was erased
    }
    (void)app.SetU32("boot_cnt", boots + 1);
}
```

The [`flux_nvs_example`](../examples/index.md) app shows strings and blobs in all three namespaces.

## Pitfalls

- **Key length.** ESP-IDF NVS keys are at most 15 characters (`GetMaxKeyLength()`).
- **`GetLastError()`** only reflects initialisation; check each `BaseNvs` call's return value.
- **No lock on initialisation.** Let `Flux::EnsureInitialized()` open NVS from one task before others use it.
