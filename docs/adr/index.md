# Decisions

Architecture decision records (ADRs): one page per decision that shapes the code, with its context, the options
considered and the consequences. A record is not rewritten after it is accepted; a later record supersedes it. The
same decisions apply to the Vortex V1 HAL, which is built the same way.

| ADR | Title | Status |
| --- | --- | --- |
| [0001](0001-mcu-agnostic-boundary.md) | Keep the MCU behind the interface wrap; only managers know the board | Accepted; construction site superseded by 0004 |
| [0002](0002-static-allocation.md) | Allocate once at start-up; fixed tables sized from the pin map | Accepted; heap allowance superseded by 0005 |
| [0003](0003-pin-map-ownership.md) | The hf-pincfg X-macro table is the single source of pin truth | Accepted |
| [0004](0004-platform-bindings.md) | Create MCU objects in a per-family platform binding; keep managers MCU-free | Accepted |
| [0005](0005-no-heap-fail-closed.md) | No heap in the HAL; managers fail closed and report through one error catalog | Accepted |
| [0006](0006-enable-pin-hard-stop.md) | A hardware enable path for both valve banks, with the hold policy left to the product | Accepted |

Start a new record from the [template](template.md).
