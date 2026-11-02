# ADR 0002: Allocate once at start-up; fixed tables sized from the pin map

| | |
| --- | --- |
| Status | Accepted; the allowance for heap-allocated handlers is superseded by [ADR 0005](0005-no-heap-fail-closed.md) |
| Date | 2026-11-01 |

## Context

The board runs for long periods controlling valves. Heap fragmentation or an allocation failure in the middle of a
control loop is hard to diagnose and can leave a valve in the wrong state. On the other hand the number of chips
that actually answer is only known at start-up, and some objects (ESP-IDF drivers) are easiest to create on the
heap.

## Options

1. **Everything static.** No heap at all; every driver object a member. Rigid, and some driver classes are not
   designed for it.
2. **Allocate once at start-up, never after.** Tables with a known size are fixed arrays; objects whose existence
   depends on start-up results are created on the heap during `EnsureInitialized()` and kept for the program's
   life.
3. **Allocate freely.** Simple, and the problems above.

## Decision

Option 2:

- Tables with a compile-time count are `std::array`, sized from the pin map or a sentinel enum:
  `GpioManager::entries_` (`HF_FUNCTIONAL_GPIO_COUNT`), `AdcManager::entries_` (`HF_FUNCTIONAL_ADC_COUNT`),
  `ValveManager::channel_states_` (`ValveChannel::CHANNEL_COUNT`), the SPI device tables
  (`SpiDeviceId::DEVICE_COUNT`), the facade's diagnostics arrays (`kFluxManagerCount`).
- Bus objects, handlers and drivers are held in `std::unique_ptr`, created during the owning manager's
  initialisation, mostly with `hf::utils::make_unique_nothrow` so a failed allocation is a logged initialisation
  failure rather than an abort.
- No manager allocates in its read, write or channel calls.

## Consequences

- After `Flux::EnsureInitialized()` the valve, ADC, temperature, GPIO and LED paths do not touch the heap.
- `GetFreeHeapBytes()` after start-up is a useful, stable number to log.

Where this is not strictly true today:

- `CommChannelsManager::AcquireUart()` creates a UART on the first call for a port, whenever the application makes
  it. Acquire UARTs during your own start-up.
- `GpioManager` and `LedManager` use `std::make_unique` (not the no-throw variant) for their pin and LED objects.
- Allocations inside ESP-IDF and the chip drivers are not covered by this decision.
