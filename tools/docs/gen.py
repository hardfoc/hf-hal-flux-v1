#!/usr/bin/env python3
"""Fill generated regions of the documentation from the code, or check that they are current.

A page marks a region with a pair of comments; everything between them is replaced::

    <!-- generated: pin-table -->
    <!-- /generated -->

Each module ``tools/docs/generators/<name>.py`` exposes ``REGIONS``, a dict from region name to a function that
returns the Markdown for it. A function that takes one argument receives a ``Page`` whose ``rel(path)`` turns a
repository path into a link relative to the page being filled. A generator reads the code (a header, a YAML file) and nothing else, so the same code
always gives the same text. Writing the source file into the region keeps the provenance on the page.

Usage::

    python3 tools/docs/gen.py            # fill every region
    python3 tools/docs/gen.py --check    # CI: fail if a region differs from what the code gives
    python3 tools/docs/gen.py --list     # regions and the files that use them
"""

from __future__ import annotations

import argparse
import importlib.util
import inspect
import os
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GENERATORS = HERE / "generators"
REGION = re.compile(r"(<!-- generated: (?P<name>[a-z0-9-]+) -->\n)(?P<body>.*?)(<!-- /generated -->)", re.S)
SKIP_DIRS = {".git", "build", "site", "node_modules", ".venv", "venv", "third_party", "_doxygen", "lib", "external"}

FENCE = re.compile(r"^(?P<indent>[ \t]*)(?P<fence>`{3,}|~{3,}).*?^(?P=indent)(?P=fence)[ \t]*$", re.M | re.S)


def outside_fences(text: str, fn) -> str:
    """Apply ``fn`` to the parts of ``text`` that are not fenced code blocks (examples of markers stay as they are)."""
    out, pos = [], 0
    for m in FENCE.finditer(text):
        out.append(fn(text[pos : m.start()]))
        out.append(m.group(0))
        pos = m.end()
    out.append(fn(text[pos:]))
    return "".join(out)


sys.path.insert(0, str(HERE))


class Page:
    """The page a region is being filled on."""

    def __init__(self, path: pathlib.Path):
        self.path = path

    def rel(self, repo_path: str) -> str:
        """A link from this page to a file of the repository."""
        return os.path.relpath(ROOT / repo_path, self.path.parent).replace(os.sep, "/")


def regions() -> dict:
    out: dict = {}
    for p in sorted(GENERATORS.glob("*.py")) if GENERATORS.exists() else []:
        if p.stem.startswith("_"):
            continue
        spec = importlib.util.spec_from_file_location(f"gen_{p.stem}", p)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for name, fn in getattr(mod, "REGIONS", {}).items():
            if name in out:
                raise SystemExit(f"[gen] region {name!r} is defined twice")
            out[name] = fn
    return out


def markdown_files() -> list[pathlib.Path]:
    out = []
    for p in sorted(ROOT.rglob("*.md")):
        rel = p.relative_to(ROOT).parts
        if any(part in SKIP_DIRS or part.startswith(".") for part in rel[:-1]):
            continue
        out.append(p)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if a generated region is stale")
    ap.add_argument("--list", action="store_true", help="list regions and where they are used")
    args = ap.parse_args()

    fns = regions()
    cache: dict[tuple[str, str], str] = {}
    bad, used = [], {}
    for p in markdown_files():
        text = p.read_text()
        if "<!-- generated:" not in text:
            continue

        def sub(m: re.Match) -> str:
            name = m.group("name")
            used.setdefault(name, []).append(str(p.relative_to(ROOT)))
            if name not in fns:
                bad.append(f"{p.relative_to(ROOT)}: no generator for region {name!r}")
                return m.group(0)
            fn = fns[name]
            required = [q for q in inspect.signature(fn).parameters.values() if q.default is q.empty]
            takes_page = len(required) == 1
            key = (name, str(p)) if takes_page else (name, "")
            if key not in cache:
                cache[key] = (fn(Page(p)) if takes_page else fn()).rstrip("\n") + "\n"
            return m.group(1) + cache[key] + m.group(4)

        new = outside_fences(text, lambda t: REGION.sub(sub, t))
        if new != text:
            if args.check:
                bad.append(f"{p.relative_to(ROOT)}: generated region is stale (run tools/docs/gen.py)")
            elif not args.list:
                p.write_text(new)
                print(f"  wrote {p.relative_to(ROOT)}")
    if args.list:
        for name in sorted(fns):
            print(f"{name:32s} {', '.join(used.get(name, ['(unused)']))}")
        return 0
    for b in bad:
        print(f"[gen] ERROR {b}", file=sys.stderr)
    if bad:
        return 1
    if args.check:
        print(f"[gen] OK: {sum(len(v) for v in used.values())} region(s) current")
    return 0


if __name__ == "__main__":
    sys.exit(main())
