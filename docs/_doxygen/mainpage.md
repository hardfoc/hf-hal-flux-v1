# Flux V1 HAL API reference

The C++ API of the HardFOC Flux V1 hardware abstraction layer: the `Flux` facade (`lib/api/Flux.h`) and one manager
per board function (`lib/managers/`).

Start with the `Flux` class: `Flux::GetInstance()` returns the board object and `EnsureInitialized()` brings every
manager up in order. Each manager class page lists its methods and the error codes they return.

The guides, the pin map and the examples are on the documentation site: https://hardfoc.github.io/hf-hal-flux-v1/latest/
