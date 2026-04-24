# Development documentation (Flux HAL)

Organization-wide **standards, process, and playbooks** now live in the **[hf-development-handbook](https://github.com/hardfoc/hf-development-handbook)** Git submodule at **[`docs/hf-development-handbook/`](../hf-development-handbook/)**.

After cloning this repository:

```bash
git submodule update --init --recursive
```

## Quick links (relative to submodule root)

| Topic | Path |
|-------|------|
| Map of all doc layers | [`DOCUMENTATION_MAP.md`](../hf-development-handbook/DOCUMENTATION_MAP.md) |
| Embedded coding standards | [`standards/embedded-coding.md`](../hf-development-handbook/standards/embedded-coding.md) |
| Coding style | [`standards/coding-style.md`](../hf-development-handbook/standards/coding-style.md) |
| CMake build contract | [`process/cmake-build-contract.md`](../hf-development-handbook/process/cmake-build-contract.md) |
| Architecture guidelines | [`process/architecture.md`](../hf-development-handbook/process/architecture.md) |
| Testing requirements | [`process/testing-requirements.md`](../hf-development-handbook/process/testing-requirements.md) |
| Agent playbooks | [`agents/README.md`](../hf-development-handbook/agents/README.md) |

Board-specific notes that truly apply **only** to Flux may be added here as additional small markdown files in the future.
