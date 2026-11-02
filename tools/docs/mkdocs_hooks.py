"""MkDocs hooks for a HardFOC documentation site (``mkdocs.yml``: ``hooks: [tools/docs/mkdocs_hooks.py]``).

What it does, page by page (``on_page_markdown``):

* **Links to the repository.** A page under ``docs/`` may link to any file of the repository (``../lib/api/Foo.h``),
  as GitHub resolves it. Those files are not part of the site, so the link is rewritten to the file on GitHub at
  the commit being built. A link that climbs out of the repository becomes plain text. Links inside ``docs/`` are
  left to MkDocs, so a broken one still fails ``mkdocs build --strict``.
* **Links to the API reference.** The Doxygen HTML is merged into the built site under ``api/`` after MkDocs runs,
  so MkDocs cannot see it. A link into ``docs/api/`` that is not a page becomes a plain ``<a>``; the link check on
  the built site (with the API merged) is what checks it.
* **Figures.** ``<!-- figure: id --> ... <!-- /figure -->`` (filled by ``tools/figkit/build.py`` with a
  ``<picture>`` for GitHub) becomes a light / dark image pair that follows the site's theme toggle, a caption and
  a collapsible long description with the sources the figure is drawn from.
* **Raw HTML paths.** ``src`` / ``srcset`` / ``href`` in raw HTML are written relative to the Markdown file, but a
  page is served one directory deeper (``guides/x.md`` becomes ``guides/x/index.html``); they are re-based.
* **Pipes in table code spans.** GitHub needs ``\\|`` inside a code span in a table; the backslash is removed.

After the build (``on_post_build``) every ``<img>`` and ``<source>`` outside ``api/`` must point at a file of the
site and every ``<img>`` must have a non-empty ``alt``; otherwise the build fails.

Nothing here names a product: the repository comes from ``GITHUB_REPOSITORY`` or ``repo_url``, the API folder
from ``extra.api_dir`` (default ``api``).
"""

from __future__ import annotations

import html
import json
import os
import posixpath
import re
import subprocess
from pathlib import Path

from mkdocs.exceptions import PluginError

ROOT = Path(__file__).resolve().parents[2]
FIGURES = ROOT / "docs/diagrams/figures.json"

LINK = re.compile(
    r"(?P<bang>!?)\[(?P<text>[^\]]*)\]\((?P<target>[^)\s]+)(?P<title>\s+\"[^\"]*\")?\)(?P<attrs>\{[^}\n]*\})?"
)
OUTSIDE = object()
CODE_SPAN = re.compile(r"(`+)(.+?)\1")
HTML_URL = re.compile(r"\b(?P<attr>src|srcset|href)=(?P<q>[\"'])(?P<val>[^\"']+)(?P=q)")
BUILT_IMG = re.compile(r"<(?:img|source)\b[^>]*?\b(?:src|srcset)=[\"']?([^\"'\s>]+)")
BUILT_IMG_TAG = re.compile(r"<img\b[^>]*>")
ALT = re.compile(r"\balt=(?:\"([^\"]*)\"|'([^']*)'|([^\s>]+))")
FIGURE = re.compile(r"<!-- figure: (?P<id>[a-z0-9-]+) -->.*?<!-- /figure -->", re.S)

_state: dict = {"repo": "", "ref": "main", "api": "api", "figures": None, "files": None}


def _ref() -> str:
    sha = os.environ.get("GITHUB_SHA")
    if sha:
        return sha
    try:
        return subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "main"


def on_config(config):
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    if not repo and config.get("repo_url"):
        m = re.search(r"github\.com/([^/]+/[^/#?]+)", config["repo_url"])
        repo = m.group(1).removesuffix(".git") if m else ""
    _state["repo"] = repo
    _state["ref"] = _ref()
    _state["api"] = (config.get("extra") or {}).get("api_dir", "api").strip("/")
    _state["figures"] = json.loads(FIGURES.read_text()) if FIGURES.exists() else {}
    return config


def _is_relative(url: str) -> bool:
    return not (re.match(r"^[a-z][a-z0-9+.-]*:", url, re.IGNORECASE) or url.startswith(("#", "/")))


def _rewrite(target: str, page_dir: str):
    """GitHub URL for a link out of docs/, OUTSIDE for one out of the repository, None to leave it to MkDocs."""
    if not _is_relative(target):
        return None
    path, _, frag = target.partition("#")
    if not path:
        return None
    resolved = posixpath.normpath(posixpath.join("docs", page_dir, path))
    if resolved.startswith(".."):
        return OUTSIDE
    if resolved == "docs" or resolved.startswith("docs/"):
        # A file under docs/ that is not part of the site (a figure module, a data file) goes to GitHub too.
        if not (_state["repo"] and (ROOT / resolved).is_file() and not _in_site(resolved[len("docs/") :])):
            return None
    if not _state["repo"]:
        return OUTSIDE
    kind = "tree" if (ROOT / resolved).is_dir() else "blob"
    url = f"https://github.com/{_state['repo']}/{kind}/{_state['ref']}/{resolved}"
    return f"{url}#{frag}" if frag else url


def _in_site(src_uri: str) -> bool:
    files = _state["files"]
    if files is None:
        return True
    f = files.get_file_from_path(src_uri)
    return f is not None and f.inclusion.is_included()


def _api_target(target: str, page_dir: str) -> bool:
    if not _is_relative(target):
        return False
    path = target.partition("#")[0]
    resolved = posixpath.normpath(posixpath.join("docs", page_dir, path))
    api = f"docs/{_state['api']}"
    return (resolved == api or resolved.startswith(api + "/")) and not (ROOT / resolved).is_file()


def _rebase(url: str, page_dir: str, out_dir: str) -> str:
    """A URL relative to the Markdown file, re-based to the HTML page MkDocs writes for it."""
    if not _is_relative(url):
        return url
    outside = _rewrite(url, page_dir)
    if isinstance(outside, str):
        return outside
    if outside is OUTSIDE:
        return url
    path, sep, frag = url.partition("#")
    rebased = posixpath.relpath(posixpath.normpath(posixpath.join(page_dir, path)), out_dir or ".")
    if path.endswith("/") and not rebased.endswith("/"):
        rebased += "/"
    return rebased + sep + frag


def _rebase_html(line: str, page_dir: str, out_dir: str) -> str:
    def attr(m: re.Match) -> str:
        val = m.group("val")
        if m.group("attr") == "srcset":
            parts = []
            for candidate in val.split(","):
                url, *descriptor = candidate.strip().split(maxsplit=1)
                parts.append(" ".join([_rebase(url, page_dir, out_dir), *descriptor]))
            val = ", ".join(parts)
        else:
            val = _rebase(val, page_dir, out_dir)
        return f"{m.group('attr')}={m.group('q')}{val}{m.group('q')}"

    pieces = CODE_SPAN.split(line)  # code spans show HTML as text; leave them alone
    out, i = [], 0
    while i < len(pieces):
        out.append(HTML_URL.sub(attr, pieces[i]))
        if i + 2 < len(pieces):
            out.append(pieces[i + 1] + pieces[i + 2] + pieces[i + 1])
        i += 3
    return "".join(out)


def _figure(m: re.Match, page_dir: str, page: str) -> str:
    fid = m.group("id")
    e = (_state["figures"] or {}).get(fid)
    if e is None:
        raise PluginError(f"{page}: figure {fid!r} is not in docs/diagrams/figures.json (run tools/figkit/build.py)")
    rel = posixpath.relpath(f"diagrams/{fid}", page_dir or ".")
    alt = html.escape(e["alt"], quote=True)
    out = [
        '<figure class="hf-figure">',
        f'<img src="{rel}.svg#only-light" alt="{alt}" width="{e["width"]}" loading="lazy">'
        f'<img src="{rel}.dark.svg#only-dark" alt="{alt}" width="{e["width"]}" loading="lazy">',
        "</figure>",
        "",
    ]
    if e.get("description") or e.get("sources"):
        out += ['<details class="hf-figure-desc" markdown="1">', "<summary>Figure description and sources</summary>", ""]
        if e.get("description"):
            out += [e["description"], ""]
        if e.get("sources"):
            root = posixpath.relpath(".", posixpath.join("docs", page_dir))
            items = []
            for s in e["sources"]:
                path = s.split(":", 1)[0]
                rest = s[len(path) + 1 :]
                items.append(f"[`{path}`]({posixpath.join(root, path)})" + (f" `{rest}`" if rest else ""))
            items.append(f"figure module [`{e['source']}`]({posixpath.join(root, e['source'])})")
            out += ["Drawn from: " + "; ".join(items) + ".", ""]
        out += ["</details>", ""]
    return "\n".join(out)


FENCE = re.compile(r"^(?P<indent>[ \t]*)(?P<fence>`{3,}|~{3,}).*?^(?P=indent)(?P=fence)[ \t]*$", re.M | re.S)


def outside_fences(text: str, fn) -> str:
    """Apply ``fn`` to the parts of ``text`` that are not fenced code blocks."""
    out, pos = [], 0
    for m in FENCE.finditer(text):
        out.append(fn(text[pos : m.start()]))
        out.append(m.group(0))
        pos = m.end()
    out.append(fn(text[pos:]))
    return "".join(out)


def on_page_markdown(markdown: str, page, config, files) -> str:
    _state["files"] = files
    page_dir = posixpath.dirname(page.file.src_uri)
    out_dir = posixpath.dirname(page.file.dest_uri)

    markdown = outside_fences(markdown, lambda t: FIGURE.sub(lambda m: _figure(m, page_dir, page.file.src_uri), t))

    def sub(m: re.Match) -> str:
        target = m.group("target")
        if not m.group("bang") and _api_target(target, page_dir):
            href = _rebase(target, page_dir, out_dir)
            classes = re.findall(r"\.([\w-]+)", m.group("attrs") or "")
            cls = f' class="{" ".join(classes)}"' if classes else ""
            return f'<a href="{href}"{cls}>{html.escape(m.group("text"), quote=False)}</a>'
        new = _rewrite(target, page_dir)
        if new is None:
            return m.group(0)
        if new is OUTSIDE:
            return m.group(0) if m.group("bang") else m.group("text")
        return f"{m.group('bang')}[{m.group('text')}]({new}{m.group('title') or ''}){m.group('attrs') or ''}"

    def unescape_pipes(m: re.Match) -> str:
        return m.group(1) + m.group(2).replace("\\|", "|") + m.group(1)

    out, in_fence = [], False
    for line in markdown.split("\n"):
        if line.lstrip().startswith(("```", "~~~")):
            in_fence = not in_fence
        if not in_fence:
            line = LINK.sub(sub, line)
            if line.lstrip().startswith("|"):
                line = CODE_SPAN.sub(unescape_pipes, line)
            if "<" in line and page_dir != out_dir:
                line = _rebase_html(line, page_dir, out_dir)
        out.append(line)
    return "\n".join(out)


def on_post_build(config) -> None:
    """Fail when a page shows an image that is not in the site, or an image without alt text."""
    site = Path(config["site_dir"])
    api = _state["api"]
    missing, no_alt = [], []
    for page in site.rglob("*.html"):
        rel = page.relative_to(site)
        if rel.parts and rel.parts[0] == api:
            continue
        text = page.read_text(errors="replace")
        for ref in BUILT_IMG.findall(text):
            url = ref.split(",")[0].split()[0]
            if not _is_relative(url) or url.startswith("data:"):
                continue
            if not (page.parent / url.split("#")[0].split("?")[0]).exists():
                missing.append(f"{rel}: {url}")
        for tag in BUILT_IMG_TAG.findall(text):
            m = ALT.search(tag)
            if not m or not any(g and g.strip() for g in m.groups()):
                no_alt.append(f"{rel}: {tag[:120]}")
    errors = []
    if missing:
        errors.append(f"{len(missing)} image(s) point at no file of the site:\n  " + "\n  ".join(missing[:20]))
    if no_alt:
        errors.append(f"{len(no_alt)} image(s) have no alt text:\n  " + "\n  ".join(no_alt[:20]))
    if errors:
        raise PluginError("\n".join(errors))
