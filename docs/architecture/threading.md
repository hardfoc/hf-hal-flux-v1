# Threading

What you can safely call from more than one FreeRTOS task. The HAL creates **no tasks of its own**: every call runs
in the task that makes it, and periodic work (such as LED animations) is driven by your code.

## Locks

Each manager has one `RtosMutex` (from `hf-utils-rtos-wrap`). It is a **recursive** mutex, so a manager method
that holds the lock may call another method of the same manager.

| Manager | `EnsureInitialized()` | Operations |
| --- | --- | --- |
| `CommChannelsManager` | Locked (double-checked) | `AcquireUart()`, `GetSystemDiagnostics()` and `Deinitialize()` lock; `GetSpiDevice()`, `GetCanBus()`, `GetUart()` and the USB serial accessors return pointers without locking |
| `NvsManager` | **Not locked** | The `BaseNvs` objects do their own work; no manager-level lock |
| `GpioManager` | Locked (double-checked) | **Not locked**: `Set()`, `Read()`, `Toggle()` and the other per-pin calls go straight to the pin |
| `ValveManager` | Locked (double-checked) | Locked: every channel, configuration and fault call |
| `AdcManager` | Locked (double-checked) | Locked: each read |
| `TemperatureManager` | Locked (double-checked) | Locked: each read |
| `LedManager` | Locked (double-checked) | Locked: colour, brightness and animation calls |
| `Flux` facade | **Not locked** | Diagnostics read atomics |

Initialisation flags are `std::atomic<bool>`, read with acquire and written with release ordering.

## What that means for your code

1. **Initialise from one task.** `Flux::EnsureInitialized()` and `NvsManager::EnsureInitialized()` have no lock;
   call `Flux::EnsureInitialized()` once from `app_main()` before you start other tasks.
2. **Valves, ADC, temperature and LED calls** may be made from several tasks; the manager serialises them. A call
   can block while another task holds the manager's lock (for example during an SPI transfer).
3. **GPIO**: give each pin one owner task, or add your own lock. Two tasks driving the same pin through
   `GpioManager` are not serialised, and the manager's operation counters are plain integers.
4. **Borrowed bus pointers** (`GetSpiDevice()`, `GetCanBus()`, `AcquireUart()`) are shared objects. The valve
   drivers already use the valve SPI bus; if you talk to a device yourself, keep to one task per device.
5. **LED animations** advance only when you call `flux.leds.UpdateAnimation()`; call it from a periodic task (the
   animation step is 50 ms).

## CPU affinity

[`FluxCpuAffinity.h`](../../lib/api/FluxCpuAffinity.h) names the logical cores your tasks should use on the
dual-core ESP32-S3: `flux_hal::kFluxCpuHostIo` (0) for host, system and communication work and
`flux_hal::kFluxCpuRealtime` (1) for high-rate loops such as pressure control. `kFluxCpuNoAffinity` (-1) lets the
scheduler choose. Pass them to `BaseThread` or `os_thread_create_pinned`.
