# Learn

A guided path for a newcomer. Read the pages in this order; each one builds on the one before, and each takes a few
minutes.

| Step | Page | You will know |
| ---: | --- | --- |
| 1 | [Home](../index.md) | What Flux V1 is and what this repository does |
| 2 | [Get started](../getting-started.md) | How to build, flash and read the start-up log |
| 3 | [Hardware](../hardware/index.md) | Which chips are on which bus, and the [pin map](../hardware/pin-map.md) |
| 4 | [Architecture](../architecture/index.md) | The layers: facade, managers, handlers, drivers, interface wrap |
| 5 | [Initialisation](../architecture/initialization.md) | What `EnsureInitialized()` does, in which order, and what can fail |
| 6 | [Ownership](../architecture/ownership.md) | Who creates and owns each handler and driver |
| 7 | [ValveManager](../managers/valves.md) | The heart of the board: solenoid and proportional channels |
| 8 | [The other managers](../managers/index.md) | ADC, temperature, GPIO, LED, comms and NVS |
| 9 | [Threading](../architecture/threading.md) and [errors](../architecture/errors.md) | What is safe to call from several tasks, and how failures are reported |
| 10 | [Examples](../examples/index.md) | Which app to run to see each feature |

When you change code, [Testing and CI](../guides/testing-and-ci.md) says what CI will check, and
[Decisions](../adr/index.md) explains why the code is shaped the way it is.
