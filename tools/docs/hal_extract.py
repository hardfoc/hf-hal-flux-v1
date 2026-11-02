"""Facts about this HAL, read from its source code: the pin map, the facade, the managers and what they use.

Every function returns plain data with the file (and symbol) it was read from, so a table or a figure built from it
can say where each fact came from, and ``--check`` can tell when the code moved. Nothing here is written by hand:
change the code, rebuild the docs (``hardfoc-docs gen``, ``hardfoc-docs figures``), commit both.

The board-specific paths are in ``BOARD`` below; everything else is shared with the other HardFOC HAL sites.
"""

from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]

BOARD = {
    "name": "Flux V1",
    "mcu": "ESP32-S3",
    "facade_header": "lib/api/Flux.h",
    "facade_source": "lib/api/Flux.cpp",
    "facade_class": "Flux",
    "init_function": "InitializeAllComponents",
    "managers_dir": "lib/managers",
    "bindings_dir": "lib/managers/bsp_bind",
    "handlers_dir": "lib/core/handlers",
    "pin_header": "lib/core/hf-core-drivers/internal/hf-pincfg/src/hf_functional_pin_config_flux_v1.hpp",
    "comms_header": "lib/managers/CommChannelsManager.h",
    "comms_source": "lib/managers/CommChannelsManager.cpp",
    "app_config": "examples/esp32/app_config.yml",
    "examples_dir": "examples/esp32/main",
}


class SourceMissing(RuntimeError):
    """A source file is not checked out (a submodule was not initialised)."""


def read(rel: str) -> str:
    p = ROOT / rel
    if not p.exists():
        raise SourceMissing(f"{rel} is missing; initialise the submodules (git submodule update --init --recursive)")
    return p.read_text(errors="replace")


def strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def split_args(s: str) -> list[str]:
    """Split a macro argument list at top-level commas (quotes and parentheses respected)."""
    out, cur, depth, quote = [], "", 0, False
    for ch in s:
        if ch == '"':
            quote = not quote
        if not quote:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                out.append(cur.strip())
                cur = ""
                continue
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def macro_entries(src: str, macro: str) -> list[list[str]]:
    """The X(...) rows of an X-macro list ``#define <macro>(X) \\ ...`` (commented-out rows are skipped)."""
    m = re.search(rf"#define\s+{macro}\(X\)\s*\\\n", src)
    if not m:
        raise ValueError(f"no #define {macro}(X)")
    body, i = [], m.end()
    for line in src[i:].split("\n"):
        body.append(line.rstrip("\\").rstrip())
        if not line.rstrip().endswith("\\"):
            break
    text = re.sub(r"/\*.*?\*/", " ", "\n".join(body), flags=re.S)
    rows = []
    for em in re.finditer(r"\bX\(", text):
        depth, j = 1, em.end()
        while depth and j < len(text):
            depth += {"(": 1, ")": -1}.get(text[j], 0)
            j += 1
        rows.append(split_args(text[em.end() : j - 1]))
    return rows


def _enum_tail(v: str) -> str:
    return v.split("::")[-1]


# ----------------------------------------------------------------------------------------------- pins


def pins() -> dict:
    """GPIO and ADC rows of the board's functional pin map (X-macro tables in the pincfg header)."""
    rel = BOARD["pin_header"]
    src = read(rel)
    gpio = []
    for r in macro_entries(src, "HF_FUNCTIONAL_GPIO_PIN_LIST"):
        name, s, cat, chip, unit, bank, pin, inv, has_pull, pull_up, push_pull, cur = r
        gpio.append(
            {
                "id": name,
                "name": s.strip('"'),
                "category": _enum_tail(cat).replace("PIN_CATEGORY_", ""),
                "chip": _enum_tail(chip),
                "unit": int(unit),
                "bank": int(bank),
                "pin": int(pin),
                "active_low": inv == "PIN_LOGIC_INVERTED",
                "pull": ("up" if pull_up == "PIN_PULL_UP" else "down") if has_pull == "PIN_HAS_PULL" else "none",
                "drive": "push-pull" if push_pull == "PIN_PUSH_PULL" else "open-drain",
                "max_ma": int(cur),
            }
        )
    adc = []
    for r in macro_entries(src, "HF_FUNCTIONAL_ADC_CHANNEL_LIST"):
        name, s, chip, unit, adc_unit, ch, res, max_mv, div, desc = r
        adc.append(
            {
                "id": name,
                "name": s.strip('"'),
                "chip": _enum_tail(chip),
                "unit": int(unit),
                "adc_unit": int(adc_unit),
                "channel": int(ch),
                "bits": int(res),
                "max_mv": int(max_mv),
                "divider": float(div.rstrip("fF")),
                "description": desc.strip('"'),
            }
        )
    return {
        "gpio": gpio,
        "adc": adc,
        "sources": [f"{rel}:HF_FUNCTIONAL_GPIO_PIN_LIST", f"{rel}:HF_FUNCTIONAL_ADC_CHANNEL_LIST"],
    }


# ----------------------------------------------------------------------------------------------- facade


def facade() -> dict:
    """Manager members of the facade class, in declaration order, and the init sequence of its init function."""
    hdr, cpp = BOARD["facade_header"], BOARD["facade_source"]
    h = strip_comments(read(hdr))
    members = [
        {"member": m.group(2), "class": m.group(1)}
        for m in re.finditer(r"^\s*(\w+(?:Manager|Controller))&\s+(\w+);", h, flags=re.M)
    ]
    by_member = {m["member"]: m["class"] for m in members}
    src = strip_comments(read(cpp))
    fn = BOARD["init_function"]
    body = _function_body(src, f"{BOARD['facade_class']}::{fn}")
    calls = []
    # Pattern A: if (!InitializeX()) { ... return false; }   (required when the block returns false)
    for m in re.finditer(r"if\s*\(\s*!\s*(Initialize\w+)\(\)\s*\)\s*\{", body):
        calls.append((m.start(), m.group(1), "return false" in _block(body, m.end() - 1)))
    # Pattern B: run_step(N, "Name", fatal, [](Flux* f) { return f->InitializeX(); })
    for m in re.finditer(r"run_step\(\s*\d+\s*,\s*\"[^\"]*\"\s*,\s*(true|false)\s*,[^;]*?->\s*(Initialize\w+)\(\)", body):
        calls.append((m.start(), m.group(2), m.group(1) == "true"))
    steps = []
    for _pos, fname, required in sorted(calls):
        init_body = _function_body(src, f"{BOARD['facade_class']}::{fname}")
        mem = re.search(r"\b(\w+)\.EnsureInitialized\(\)", init_body)
        member = mem.group(1) if mem else ""
        steps.append({"function": fname, "member": member, "class": by_member.get(member, ""), "required": required})
    return {
        "members": members,
        "init": steps,
        "sources": [f"{hdr}:{BOARD['facade_class']} (manager references)", f"{cpp}:{BOARD['facade_class']}::{fn}"],
    }


def _block(src: str, brace: int) -> str:
    depth, j = 0, brace
    while j < len(src):
        depth += {"{": 1, "}": -1}.get(src[j], 0)
        j += 1
        if depth == 0:
            break
    return src[brace:j]


def _function_body(src: str, qualified: str) -> str:
    m = re.search(re.escape(qualified) + r"\s*\([^)]*\)[^{;]*\{", src)
    if not m:
        raise ValueError(f"no definition of {qualified}")
    return _block(src, m.end() - 1)


# ----------------------------------------------------------------------------------------------- includes

INCLUDE = re.compile(r'#include\s+"([^"]+)"')


def _includes(paths: list[pathlib.Path]) -> list[str]:
    out = []
    for p in paths:
        out += INCLUDE.findall(strip_comments(p.read_text(errors="replace")).replace("#include", "\n#include"))
    return out


def dependencies() -> dict:
    """What each manager uses (other managers, hf-core handlers, drivers, interface classes), from its #includes,
    and which driver each handler wraps."""
    mdir = ROOT / BOARD["managers_dir"]
    hdir = ROOT / BOARD["handlers_dir"]
    if not hdir.exists():
        raise SourceMissing(f"{BOARD['handlers_dir']} is missing; initialise lib/core")
    managers = sorted(p.stem for p in mdir.glob("*.h"))
    # A handler header may be included by its bare name ("Ads7952Handler.h") through the include path.
    by_file = {p.name: p.parent.name for p in hdir.glob("*/*.h") if p.parent.name not in ("logger", "common")}
    out = {}
    for name in managers:
        files = sorted(mdir.glob(f"{name}.*"))
        inc = _includes(files)
        handlers = sorted(
            {m.group(1) for i in inc if (m := re.match(r"handlers/(\w+)/", i)) and m.group(1) != "logger"}
            | {by_file[i] for i in inc if "/" not in i and i in by_file}
        )
        drivers = sorted({m.group(1) for i in inc if (m := re.search(r"external/(hf-[\w-]+?)(?:-driver)?/", i))})
        iface = sorted({m.group(1) for i in inc if (m := re.search(r"(?:^|/)(?:base|mcu/\w+)/(\w+)\.h$", i))}
                       - {"HardwareTypes"})
        uses = sorted({i[:-2] for i in inc if i.endswith(".h") and i[:-2] in managers and i[:-2] != name})
        out[name] = {
            "handlers": handlers,
            "drivers": drivers,
            "interfaces": iface,
            "managers": uses,
            "pin_map": any("hf_functional_pin_config" in i for i in inc),
            "files": [str(f.relative_to(ROOT)) for f in files],
        }
    handler_drivers = {}
    for h in sorted({h for m in out.values() for h in m["handlers"]}):
        inc = _includes(sorted((hdir / h).glob("*.h")))
        handler_drivers[h] = sorted(
            {m.group(1) for i in inc if (m := re.search(r"external/(hf-[\w-]+?)(?:-driver)?/", i))}
        )
    return {
        "managers": out,
        "handler_drivers": handler_drivers,
        # "path:what" so the docs can link the path; the paths are folders.
        "sources": [f"{BOARD['managers_dir']}:#includes", f"{BOARD['handlers_dir']}:#includes"],
    }


def bindings() -> dict:
    """The platform-binding families under ``bsp_bind/`` (one directory per MCU family; ``board`` is board data, not
    a family) and, from each family's #includes, the hf-core interface implementations and chip drivers it creates."""
    bdir = ROOT / BOARD["bindings_dir"]
    families = {}
    for d in sorted(p for p in bdir.iterdir() if p.is_dir() and p.name != "board"):
        inc = _includes(sorted(d.glob("*.cpp")) + sorted(d.glob("*.h")))
        families[d.name] = {
            "classes": sorted({m.group(1) for i in inc if (m := re.search(r"mcu/\w+/(\w+)\.h$", i))}),
            "drivers": sorted({m.group(1) for i in inc if (m := re.search(r"external/(hf-[\w-]+?)(?:-driver)?/", i))}),
            "files": [str(f.relative_to(ROOT)) for f in sorted(d.glob("*.*"))],
        }
    return {
        "interface": f"{BOARD['bindings_dir']}/FluxPlatformBind.h",
        "families": families,
        "sources": [f"{BOARD['bindings_dir']}:#includes"],
    }


# ----------------------------------------------------------------------------------------------- buses


def buses() -> dict:
    """Devices on each bus: SPI chip selects and I2C built-in devices from the comms manager, pins from the pin map."""
    hdr, cpp = BOARD["comms_header"], BOARD["comms_source"]
    h = read(hdr)
    spi = []
    em = re.search(r"enum class SpiDeviceId[^{]*\{(.*?)\};", h, flags=re.S)
    for m in re.finditer(r"^\s*(\w+)\s*=\s*\d+\s*,\s*///<\s*(.*)$", em.group(1) if em else "", flags=re.M):
        if m.group(1) != "DEVICE_COUNT":
            mode = re.search(r"Mode (\d)", m.group(2))
            spi.append({"id": m.group(1), "label": re.sub(r"\s*\(.*?\)\s*$", "", m.group(2)).strip(),
                        "mode": int(mode.group(1)) if mode else None})
    c = strip_comments(read(cpp))
    i2c = [
        {"id": m.group(1), "address": m.group(2), "label": m.group(3)}
        for m in re.finditer(r"add_builtin_device\(\s*I2cDeviceId::(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*\"([^\"]+)\"", c)
    ]
    comm = [p for p in pins()["gpio"] if p["category"] == "COMM"]
    return {
        "spi": spi,
        "i2c": i2c,
        "comm_pins": comm,
        "sources": [f"{hdr}:SpiDeviceId", f"{cpp}:add_builtin_device", f"{BOARD['pin_header']}:COMM pins"],
    }


# ----------------------------------------------------------------------------------------------- examples


def examples() -> dict:
    """Example apps from app_config.yml with the @brief of each source file."""
    import yaml

    rel = BOARD["app_config"]
    cfg = yaml.safe_load(read(rel))
    apps = []
    for name, a in cfg["apps"].items():
        src = a.get("source_file") or a["source_files"][0]
        src_rel = f"{BOARD['examples_dir']}/{src}"
        apps.append(
            {
                "name": name,
                "source": src_rel,
                "category": a.get("category", ""),
                "ci": bool(a.get("ci_enabled", False)),
                "brief": a.get("description") or brief(read(src_rel)),
            }
        )
    return {
        "metadata": cfg.get("metadata", {}),
        "apps": apps,
        "sources": [rel, f"{BOARD['examples_dir']}/*.cpp (@brief)"],
    }


def brief(src: str) -> str:
    """The first @brief paragraph of a file's Doxygen header, on one line."""
    m = re.search(r"@brief\s+(.*?)(?:\n\s*\*\s*\n|\n\s*\*\s*@|\*/)", src, flags=re.S)
    if not m:
        return ""
    text = re.sub(r"\n\s*\*\s?", " ", m.group(1))
    return " ".join(text.replace("**", "").split())
