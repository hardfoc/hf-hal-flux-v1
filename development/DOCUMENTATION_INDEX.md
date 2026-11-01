# HardFOC Flux HAL — documentation index

Valve-control HAL for **Flux V1**. Public entry: **`Flux`** in [`lib/api/Flux.h`](lib/api/Flux.h).

## Start here

| Document | Purpose |
|----------|---------|
| [README.md](README.md) | Overview and quick start |
| [`docs/hal/README.md`](docs/hal/README.md) | **Canonical** doc hub — architecture, managers, example links |
| [`docs/hal/architecture.md`](docs/hal/architecture.md) | Init policy (fatal vs best-effort), shutdown |
| [`docs/hal/managers-and-handlers.md`](docs/hal/managers-and-handlers.md) | `flux.*` ↔ managers ↔ `hf-core` |
| [`examples/esp32/docs/README.md`](examples/esp32/docs/README.md) | Every `APP_TYPE` and source file |
| [`lib/api/README.md`](lib/api/README.md) | API integration notes |

## Handbook submodule

[`docs/hf-development-handbook/README.md`](docs/hf-development-handbook/README.md)

## hf-core published docs

[hf-core GitHub Pages](https://hardfoc.github.io/hf-core/)

## Usage pattern

```cpp
#include "api/Flux.h"

extern "C" void app_main(void) {
    auto& flux = Flux::GetInstance();
    if (!flux.EnsureInitialized()) {
        return;
    }
    auto& valves = flux.valves;
    (void)valves;
}
```

`EnsureInitialized()` fails only when **NVS, comms, or GPIO** does not come up; other managers are best-effort (see [`docs/hal/architecture.md`](docs/hal/architecture.md)).

## Former topic-doc folders

[`docs/component-handlers/README.md`](docs/component-handlers/README.md) and [`docs/driver-handlers/README.md`](docs/driver-handlers/README.md) — redirect stubs only (old `*_README.md` files removed).
