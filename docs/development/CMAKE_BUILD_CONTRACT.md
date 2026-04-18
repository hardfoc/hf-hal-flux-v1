---
layout: default
title: "⚙️ CMake Build Contract"
description: "Standardized CMake architecture for all HardFOC drivers"
nav_order: 9
parent: "🏗️ Development"
permalink: /docs/development/cmake_build_contract/
---

# CMake Build Contract

This document defines the **standard CMake architecture** that all HardFOC
drivers must follow.  It exists at the HAL level because the pattern is shared
across every driver submodule — any driver-specific integration details live in
that driver's own `docs/cmake_integration.md`.

---

## Design Goals

| Goal | How the Contract Achieves It |
|------|------------------------------|
| **Single source of truth** | Version, target name, sources, and includes are defined once in `build_settings.cmake` |
| **Multi-build-system** | One settings file feeds both generic CMake and ESP-IDF component wrappers |
| **Zero duplication** | Root `CMakeLists.txt` and ESP-IDF wrapper both consume shared variables — no copy/paste |
| **Header-only ↔ compiled** | Adding files to `SOURCE_FILES` is the only change needed to switch modes |
| **Parallel-build safe** | Generated headers go in `CMAKE_CURRENT_BINARY_DIR`, not the source tree |
| **Self-contained submodule** | Each driver works independently when cloned outside the HAL monorepo |

---

## 3-Layer Architecture

Every driver ships exactly **three CMake files** that form a layered contract:

```
┌──────────────────────────────────────────────────────────────┐
│              cmake/hf_<driver>_build_settings.cmake          │
│                     (Single Source of Truth)                  │
│                                                              │
│  • Version (MAJOR.MINOR.PATCH)                               │
│  • Target name (HF_<DRIVER>_TARGET_NAME)                     │
│  • Public include directories                                │
│  • Source file list (empty for header-only)                   │
│  • ESP-IDF component dependencies                            │
│  • Version header generation (configure_file)                │
└────────────────┬───────────────────────────┬─────────────────┘
                 │                           │
    ┌────────────▼───────────┐  ┌────────────▼────────────────┐
    │   CMakeLists.txt       │  │  components/hf_<driver>/    │
    │   (Root / Generic)     │  │  CMakeLists.txt             │
    │                        │  │  (ESP-IDF Wrapper)          │
    │ • INTERFACE / STATIC   │  │                             │
    │ • Namespace alias      │  │ • idf_component_register()  │
    │ • install() / export() │  │ • Maps shared vars to IDF   │
    │ • find_package() support│ │ • C++20 compile feature     │
    └────────────────────────┘  └─────────────────────────────┘
```

### Layer 1 — `build_settings.cmake` (shared variables)

This is the **only file that defines version, sources, and includes**.  Both
downstream layers include it and read the exported variables.

**Required variables** (replace `<DRIVER>` with your driver's uppercase name):

| Variable | Purpose | Example |
|----------|---------|---------|
| `HF_<DRIVER>_TARGET_NAME` | CMake target name | `hf_max22200` |
| `HF_<DRIVER>_VERSION` | Full semver string | `1.2.0` |
| `HF_<DRIVER>_VERSION_MAJOR` | Major version | `1` |
| `HF_<DRIVER>_VERSION_MINOR` | Minor version | `2` |
| `HF_<DRIVER>_VERSION_PATCH` | Patch version | `0` |
| `HF_<DRIVER>_PUBLIC_INCLUDE_DIRS` | Public header paths (source + generated) | `inc/;${generated_dir}` |
| `HF_<DRIVER>_SOURCE_FILES` | Source files (empty list = header-only) | `""` or `"src/driver.cpp"` |
| `HF_<DRIVER>_IDF_REQUIRES` | ESP-IDF component deps | `"driver"` |
| `HF_<DRIVER>_VERSION_HEADER_DIR` | Directory for generated version header | `${CMAKE_CURRENT_BINARY_DIR}/...` |
| `HF_<DRIVER>_VERSION_HEADER` | Full path to generated header | `${header_dir}/<driver>_version.h` |

**Build settings template:**

```cmake
# cmake/hf_<driver>_build_settings.cmake
# ─── Single Source of Truth ───────────────────────────────

# ── Guard: only process once per configure ──
if(DEFINED HF_<DRIVER>_BUILD_SETTINGS_INCLUDED)
    return()
endif()
set(HF_<DRIVER>_BUILD_SETTINGS_INCLUDED TRUE)

# ── Version ──
set(HF_<DRIVER>_VERSION_MAJOR 1)
set(HF_<DRIVER>_VERSION_MINOR 0)
set(HF_<DRIVER>_VERSION_PATCH 0)
set(HF_<DRIVER>_VERSION
    "${HF_<DRIVER>_VERSION_MAJOR}.${HF_<DRIVER>_VERSION_MINOR}.${HF_<DRIVER>_VERSION_PATCH}")

# ── Target name ──
set(HF_<DRIVER>_TARGET_NAME "hf_<driver>")

# ── Driver root (auto-detect from this file's location) ──
get_filename_component(HF_<DRIVER>_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Allow override: cmake -DHF_<DRIVER>_ROOT=/custom/path
set(HF_<DRIVER>_ROOT "${HF_<DRIVER>_ROOT}" CACHE PATH
    "Root directory of hf-<driver>-driver")

# ── Version header generation ──
set(HF_<DRIVER>_VERSION_TEMPLATE "${HF_<DRIVER>_ROOT}/inc/<driver>_version.h.in")
set(HF_<DRIVER>_VERSION_HEADER_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/${HF_<DRIVER>_TARGET_NAME}_generated")
set(HF_<DRIVER>_VERSION_HEADER
    "${HF_<DRIVER>_VERSION_HEADER_DIR}/<driver>_version.h")

# Generate the version header into the build directory
file(MAKE_DIRECTORY "${HF_<DRIVER>_VERSION_HEADER_DIR}")
configure_file(
    "${HF_<DRIVER>_VERSION_TEMPLATE}"
    "${HF_<DRIVER>_VERSION_HEADER}"
    @ONLY
)

# ── Public includes ──
set(HF_<DRIVER>_PUBLIC_INCLUDE_DIRS
    "${HF_<DRIVER>_ROOT}/inc"
    "${HF_<DRIVER>_VERSION_HEADER_DIR}"
)

# ── Sources (empty = header-only / INTERFACE) ──
set(HF_<DRIVER>_SOURCE_FILES "")

# ── ESP-IDF dependencies ──
set(HF_<DRIVER>_IDF_REQUIRES "driver")
```

### Layer 2a — Root `CMakeLists.txt` (generic CMake)

Consumed by `add_subdirectory()` or `find_package()`.

```cmake
cmake_minimum_required(VERSION 3.16)
project(hf_<driver> LANGUAGES CXX)

# ── Load shared settings ──
include("${CMAKE_CURRENT_LIST_DIR}/cmake/hf_<driver>_build_settings.cmake")

# ── Create target (INTERFACE for header-only, STATIC for compiled) ──
if(HF_<DRIVER>_SOURCE_FILES)
    add_library(${HF_<DRIVER>_TARGET_NAME} STATIC ${HF_<DRIVER>_SOURCE_FILES})
    target_compile_features(${HF_<DRIVER>_TARGET_NAME} PUBLIC cxx_std_20)
else()
    add_library(${HF_<DRIVER>_TARGET_NAME} INTERFACE)
    target_compile_features(${HF_<DRIVER>_TARGET_NAME} INTERFACE cxx_std_20)
endif()

# ── Public includes ──
target_include_directories(${HF_<DRIVER>_TARGET_NAME}
    INTERFACE ${HF_<DRIVER>_PUBLIC_INCLUDE_DIRS}
)

# ── Namespace alias ──
add_library(hf::<driver> ALIAS ${HF_<DRIVER>_TARGET_NAME})

# ── Optional: compiler warnings ──
option(HF_<DRIVER>_ENABLE_WARNINGS "Enable strict warnings for ${HF_<DRIVER>_TARGET_NAME}" OFF)
if(HF_<DRIVER>_ENABLE_WARNINGS)
    target_compile_options(${HF_<DRIVER>_TARGET_NAME} INTERFACE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()
```

### Layer 2b — ESP-IDF Component Wrapper

Located at `examples/esp32/components/hf_<driver>/CMakeLists.txt` (or wherever
your ESP-IDF project references it).

```cmake
# ESP-IDF component wrapper — delegates to build_settings
# NOTE: Do NOT add cmake_minimum_required() here (ESP-IDF manages this)

# ── Determine driver root (relative to component location) ──
get_filename_component(_driver_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(HF_<DRIVER>_ROOT "${_driver_root}" CACHE PATH
    "Root directory of hf-<driver>-driver (override if layout differs)")

# ── Load shared settings ──
include("${HF_<DRIVER>_ROOT}/cmake/hf_<driver>_build_settings.cmake")

# ── Register as ESP-IDF component ──
idf_component_register(
    SRCS ${HF_<DRIVER>_SOURCE_FILES}
    INCLUDE_DIRS ${HF_<DRIVER>_PUBLIC_INCLUDE_DIRS}
    REQUIRES ${HF_<DRIVER>_IDF_REQUIRES}
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
```

---

## Version Header Pattern

Every driver includes a `inc/<driver>_version.h.in` template that CMake
processes via `configure_file()`:

```c
// inc/<driver>_version.h.in — DO NOT EDIT (generated at build time)
#ifndef HF_<DRIVER>_VERSION_H
#define HF_<DRIVER>_VERSION_H

#define HF_<DRIVER>_VERSION_MAJOR  @HF_<DRIVER>_VERSION_MAJOR@
#define HF_<DRIVER>_VERSION_MINOR  @HF_<DRIVER>_VERSION_MINOR@
#define HF_<DRIVER>_VERSION_PATCH  @HF_<DRIVER>_VERSION_PATCH@
#define HF_<DRIVER>_VERSION_STRING "@HF_<DRIVER>_VERSION@"

#endif
```

The generated header is placed in `${CMAKE_CURRENT_BINARY_DIR}/<target>_generated/`
to keep the source tree clean and support parallel builds with different
configurations.

---

## Variable Naming Convention

All variables follow the pattern `HF_<DRIVER>_<FIELD>`:

- **Prefix**: Always `HF_` (HardFOC namespace)
- **Driver**: Uppercase driver name (e.g. `MAX22200`, `TLE92466ED`, `ADS7952`)
- **Field**: Describes the purpose (e.g. `VERSION_MAJOR`, `SOURCE_FILES`)

This ensures no collisions between drivers when multiple are included in the
same CMake build tree.

---

## Porting Checklist — New Driver

To create a new driver following this contract:

### 1. Copy the Three Files

```
your-new-driver/
├── cmake/
│   └── hf_<driver>_build_settings.cmake    ← from template above
├── CMakeLists.txt                           ← from template above
├── inc/
│   ├── <driver>.hpp
│   └── <driver>_version.h.in               ← from template above
├── src/
│   └── <driver>.ipp                         ← (or .cpp for compiled drivers)
└── examples/
    └── esp32/
        └── components/
            └── hf_<driver>/
                └── CMakeLists.txt           ← from template above
```

### 2. Find-Replace Variables

In all three CMake files, replace:

| Find | Replace with |
|------|--------------|
| `<DRIVER>` | Your driver's uppercase name (e.g. `MAX22200`) |
| `<driver>` | Your driver's lowercase name (e.g. `max22200`) |

Update:
- Version numbers
- Include directories (if your layout differs from `inc/`)
- Source file list (if the driver is compiled, not header-only)
- ESP-IDF `REQUIRES` dependencies

### 3. Create the Version Template

Copy the `.h.in` pattern from the Version Header Pattern section above.

### 4. Add Driver-Specific `docs/cmake_integration.md`

Document the **consumer-facing** integration steps specific to your driver:
- Target name and link command
- ESP-IDF component setup with correct paths
- Any driver-specific CMake options

Link back to this document for the full contract architecture:

```markdown
> For the complete 3-layer CMake build contract architecture, variable naming
> conventions, and porting guide, see the
> [HAL-level CMake Build Contract](../../docs/development/CMAKE_BUILD_CONTRACT.md).
```

### 5. Verify

```bash
# Generic CMake build
cmake -B build && cmake --build build

# ESP-IDF build (from examples/esp32/)
./scripts/build_app.sh <example_name> Debug --clean
```

---

## Header-Only vs Compiled Drivers

The contract supports both modes transparently:

| Mode | `SOURCE_FILES` | CMake Target Type | Consumer Impact |
|------|---------------|-------------------|-----------------|
| **Header-only** | `""` (empty) | `INTERFACE` | Zero link cost, all inlined |
| **Compiled** | `"src/foo.cpp"` | `STATIC` | Links a `.a`, smaller binary if not all used |

To switch a header-only driver to compiled:

1. Add source files to `HF_<DRIVER>_SOURCE_FILES` in `build_settings.cmake`
2. Done — both the root `CMakeLists.txt` and ESP-IDF wrapper adapt automatically

---

## Cross-Reference Comments

Each CMake file should include a comment pointing to the others:

```cmake
# ── Cross-references ──────────────────────────────
# Build settings: cmake/hf_<driver>_build_settings.cmake
# ESP-IDF wrapper: examples/esp32/components/hf_<driver>/CMakeLists.txt
# Build contract:  <hal-root>/docs/development/CMAKE_BUILD_CONTRACT.md
```

This helps developers navigate the 3-layer structure without memorizing the
layout.

---

**Navigation**
⬅️ [Back to Architecture Guidelines](ARCHITECTURE_GUIDELINES.md) | [Documentation Index](../../DOCUMENTATION_INDEX.md)
