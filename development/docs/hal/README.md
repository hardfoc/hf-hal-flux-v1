# Flux HAL — documentation hub

Canonical navigation for **Flux V1**: how the **`Flux`** façade (`lib/api/`) composes **board managers** in `lib/managers/` with **handlers and drivers** from **`hf-core`** (`lib/core/` submodule).

## Read next

| Document | What it covers |
|----------|----------------|
| [architecture.md](architecture.md) | Init policy (fatal vs best-effort steps), shutdown order, diagnostics |
| [managers-and-handlers.md](managers-and-handlers.md) | Each `flux.*` member, manager headers, valve/ADC stack pointers into `hf-core` |
| [../../examples/esp32/docs/README.md](../../examples/esp32/docs/README.md) | Every `APP_TYPE` from `app_config.yml`, entry sources, CI flags |

## Cross-repo references

- **Handbook:** [`../hf-development-handbook/README.md`](../hf-development-handbook/README.md), especially [layered architecture](../hf-development-handbook/process/layered-architecture.md).
- **hf-core published docs:** [GitHub Pages](https://hardfoc.github.io/hf-core/).
- **API:** [`../../lib/api/Flux.h`](../../lib/api/Flux.h), [`../../lib/api/README.md`](../../lib/api/README.md).

## Former `docs/component-handlers/` and `docs/driver-handlers/`

Old per-topic READMEs were removed. See [`../component-handlers/README.md`](../component-handlers/README.md) / [`../driver-handlers/README.md`](../driver-handlers/README.md) for the redirect note only.
