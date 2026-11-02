"""figkit core: hand-placed SVG figures, light and dark from one layout, checked as they are drawn.

* **Tokens, not hex.** Every colour is a token from ``theme.py``; ``Fig.svg(theme)`` resolves them, so one placement
  renders as ``<id>.svg`` (light) and ``<id>.dark.svg`` (dark).
* **Checked as it is drawn.** Text is measured with real font metrics (``metrics.json``); every text records the
  paint under it, and ``Fig.problems()`` reports contrast below 4.5:1 in either theme, text that does not fit its
  box, and cards that overlap. ``build.py`` refuses a figure with problems.
* **Accessible.** Every figure carries a one-sentence ``alt`` (required) that becomes the SVG ``<title>`` and the
  ``alt`` of every embed; the module docstring's ``Reads:`` paragraph becomes the long description.
* **Deterministic.** The same input gives the same bytes, so ``build.py --check`` can compare committed output.

Coordinates passed to drawing calls are *body* coordinates: (0, 0) is the top-left corner of the area under the
figure header, inside the side padding.
"""

from __future__ import annotations

import json
import math
import pathlib
from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field
from xml.sax.saxutils import escape

from . import theme as T

SANS = "Inter, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif"
MONO = "'JetBrains Mono', 'SFMono-Regular', Menlo, Consolas, monospace"

_METRICS = json.loads((pathlib.Path(__file__).with_name("metrics.json")).read_text())["fonts"]

#: Contrast every text must reach against the paint under it, in both themes.
MIN_CONTRAST = 4.5
#: Width at which GitHub shows a README image; an ``overview`` figure keeps text >= 11 px there.
READ_WIDTH = 830
LAYERS = ("bg", "region", "edge", "node", "label")

PROFILES = {
    # Deep pages: shown at full content width (about 1000-1200 px).
    "detail": dict(
        title=26, subtitle=15, kicker=12.5, card_title=15, card_body=13, mono=12.5, region=13, pill=11.5,
        small=12, edge_label=12, note=13, legend=12.5, pad=14, rx=10, stroke=1.6,
    ),
    # Landing pages and READMEs: shown about 830 px wide, so no text below 16 px at 1200 px wide.
    "overview": dict(
        title=32, subtitle=18, kicker=16, card_title=20, card_body=17, mono=16, region=17, pill=16,
        small=16, edge_label=16, note=17, legend=16, pad=18, rx=12, stroke=2.2,
    ),
}


def C(token: str) -> str:
    """Placeholder for a colour token, resolved by ``Fig.svg(theme)``."""
    T.resolve(token, "light")  # fail early on an unknown token
    return f"§{token}§"


def text_width(s: str, size: float, weight: int = 400, mono: bool = False) -> float:
    """Width of ``s`` in px, from Inter / JetBrains Mono advances plus a 6 % margin for fallback fonts."""
    if mono:
        key = "mono-400"
    else:
        key = f"sans-{min((400, 500, 600, 700), key=lambda w: abs(w - weight))}"
    m = _METRICS[key]
    em = sum(m["widths"].get(ch, m["default"]) for ch in s)
    return em * size * 1.06


def wrap(s: str, width: float, size: float, weight: int = 400, mono: bool = False) -> list[str]:
    """Greedy word wrap; explicit newlines are kept."""
    out: list[str] = []
    for para in s.split("\n"):
        cur = ""
        for word in para.split(" "):
            cand = word if not cur else f"{cur} {word}"
            if cur and text_width(cand, size, weight, mono) > width:
                out.append(cur)
                cur = word
            else:
                cur = cand
        out.append(cur)
    return out


def _f(v: float) -> str:
    r = round(v, 1)
    return str(int(r)) if r == int(r) else f"{r:.1f}"


@dataclass
class Box:
    x: float
    y: float
    w: float
    h: float

    @property
    def cx(self) -> float:
        return self.x + self.w / 2

    @property
    def cy(self) -> float:
        return self.y + self.h / 2

    @property
    def r(self) -> float:
        return self.x + self.w

    @property
    def b(self) -> float:
        return self.y + self.h

    def port(self, side: str, t: float = 0.5) -> tuple[float, float]:
        """A point on one side: ``t`` runs 0..1 left to right (top / bottom) or top to bottom (left / right)."""
        if side == "top":
            return (self.x + self.w * t, self.y)
        if side == "bottom":
            return (self.x + self.w * t, self.b)
        if side == "left":
            return (self.x, self.y + self.h * t)
        if side == "right":
            return (self.r, self.y + self.h * t)
        raise ValueError(side)

    def overlaps(self, o: Box, margin: float = 0.0) -> bool:
        return not (
            self.r + margin <= o.x or o.r + margin <= self.x or self.b + margin <= o.y or o.b + margin <= self.y
        )

    def inside(self, o: Box) -> bool:
        return self.x >= o.x - 0.5 and self.y >= o.y - 0.5 and self.r <= o.r + 0.5 and self.b <= o.b + 0.5


@dataclass
class Fig:
    """One figure. ``alt`` is one sentence saying what the figure shows; it is required."""

    fid: str
    title: str
    alt: str
    width: int = 1200
    subtitle: str = ""
    kicker: str = ""
    profile: str = "detail"
    legend: list = field(default_factory=list)
    source: str = ""
    doc: str = ""

    def __post_init__(self) -> None:
        self.k = PROFILES[self.profile]
        self.pad = 32
        self.body_w = self.width - 2 * self.pad
        self._layers: dict[str, list[str]] = {name: [] for name in LAYERS}
        self._texts: list[tuple[Box, str, str, str, float]] = []  # (box, colour, background, text, size)
        self._cards: list[tuple[Box, str]] = []
        self._problems: list[str] = []
        self._markers: set[str] = set()
        self._segments: list[tuple[tuple[float, float], tuple[float, float]]] = []
        self._labels: list[tuple[Box, str]] = []
        self._maxy = 0.0
        self._min_size = 99.0
        sub = wrap(self.subtitle, self.body_w, self.k["subtitle"]) if self.subtitle else []
        self._sub_lines = sub
        top = 34 + (self.k["kicker"] + 10 if self.kicker else 0)
        self._title_y = top + self.k["title"] * 0.8
        self._sub_y = self._title_y + 10 + self.k["subtitle"] * 1.1
        self.oy = (self._sub_y + (len(sub) - 1) * self.k["subtitle"] * 1.45 if sub else self._title_y) + 34

    # ------------------------------------------------------------------ bookkeeping

    def _put(self, layer: str, s: str) -> None:
        self._layers[layer].append(s)

    def _X(self, x: float) -> float:
        return x + self.pad

    def _Y(self, y: float) -> float:
        return y + self.oy

    def _grow(self, y: float) -> None:
        self._maxy = max(self._maxy, y)

    def problem(self, msg: str) -> None:
        self._problems.append(msg)

    @property
    def min_text_px(self) -> float:
        return self._min_size

    @property
    def min_display_px(self) -> float:
        """Smallest text as shown at README width (``READ_WIDTH``)."""
        return round(self._min_size * min(1.0, READ_WIDTH / self.width), 2)

    # ------------------------------------------------------------------ primitives

    def text(
        self,
        x: float,
        y: float,
        s: str,
        size: float | None = None,
        weight: int = 400,
        color: str = "ink",
        bg: str = "canvas",
        anchor: str = "start",
        mono: bool = False,
        layer: str = "label",
        header: bool = False,
    ) -> Box:
        """One line of text with its baseline at ``y``. ``bg`` is the paint under it, for the contrast check."""
        size = size or self.k["card_body"]
        w = text_width(s, size, weight, mono)
        x0 = x if anchor == "start" else (x - w / 2 if anchor == "middle" else x - w)
        bb = Box(x0, y - size * 0.8, w, size * 1.05)
        if s.strip():
            self._min_size = min(self._min_size, size)
            self._texts.append((bb, color, bg, s, size))
        if not header:
            self._grow(y + size * 0.3)
        X = self._X(x) if not header else x + self.pad
        Y = self._Y(y) if not header else y
        fam = f' font-family="{MONO}"' if mono else ""
        anc = "" if anchor == "start" else f' text-anchor="{anchor}"'
        wt = "" if weight == 400 else f' font-weight="{weight}"'
        self._put(
            layer,
            f'<text x="{_f(X)}" y="{_f(Y)}" font-size="{_f(size)}"{wt}{fam}{anc} fill="{C(color)}">{escape(s)}</text>',
        )
        return bb

    def rect(
        self,
        x: float,
        y: float,
        w: float,
        h: float,
        fill: str = "card",
        stroke: str | None = "line",
        rx: float | None = None,
        sw: float = 1.0,
        dash: str | None = None,
        layer: str = "node",
    ) -> Box:
        rx = self.k["rx"] if rx is None else rx
        st = f' stroke="{C(stroke)}" stroke-width="{_f(sw)}"' if stroke else ""
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self._put(
            layer,
            f'<rect x="{_f(self._X(x))}" y="{_f(self._Y(y))}" width="{_f(w)}" height="{_f(h)}" rx="{_f(rx)}" '
            f'fill="{C(fill)}"{st}{da}/>',
        )
        self._grow(y + h)
        return Box(x, y, w, h)

    def line(self, pts: Sequence[tuple[float, float]], color: str = "rail", sw: float = 1.0, dash=None, layer="bg"):
        d = " ".join(f"{'M' if i == 0 else 'L'}{_f(self._X(px))} {_f(self._Y(py))}" for i, (px, py) in enumerate(pts))
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self._put(layer, f'<path d="{d}" fill="none" stroke="{C(color)}" stroke-width="{_f(sw)}"{da}/>')

    # ------------------------------------------------------------------ building blocks

    def region(
        self,
        x: float,
        y: float,
        w: float,
        h: float,
        label: str,
        tone: str = "slate",
        sub: str = "",
        dashed: bool = False,
    ) -> Box:
        """A tinted band that groups cards; the label sits in its top-left corner."""
        self.rect(x, y, w, h, fill=f"{tone}.tint", stroke=f"{tone}.soft", sw=1.2, dash="6 4" if dashed else None,
                  layer="region", rx=self.k["rx"] + 4)
        size = self.k["region"]
        bb = self.text(x + 16, y + 12 + size, label.upper(), size=size, weight=700, color=f"{tone}.text",
                       bg=f"{tone}.tint", layer="label")
        if sub:
            self.text(bb.r + 12, y + 12 + size, sub, size=self.k["small"], color=f"{tone}.text", bg=f"{tone}.tint")
        if bb.r > x + w - 8:
            self.problem(f"region label {label!r} is wider than its region")
        return Box(x, y, w, h)

    def region_inner_top(self, y: float) -> float:
        """Body y under a region label, where its first card can start."""
        return y + 12 + self.k["region"] + 16

    def pill(self, x: float, y: float, s: str, tone: str = "slate", solid: bool = False, layer: str = "label",
             on: str = "card") -> Box:
        """A small rounded label; ``y`` is its top. ``on`` is the paint under an outline pill."""
        size = self.k["pill"]
        w = text_width(s, size, 700) + 16
        h = size + 10
        fill = f"{tone}.text" if solid else f"{tone}.tint"
        self.rect(x, y, w, h, fill=fill, stroke=None if solid else f"{tone}.soft", rx=h / 2, layer=layer)
        self.text(x + 8, y + 5 + size * 0.82, s, size=size, weight=700, color="on" if solid else f"{tone}.text",
                  bg=fill, layer=layer)
        return Box(x, y, w, h)

    def pill_width(self, s: str) -> float:
        return text_width(s, self.k["pill"], 700) + 16

    def chips(self, x: float, y: float, w: float, items: Iterable[str], tone: str = "slate", on: str = "card",
              gap: float = 8, solid: bool = False) -> float:
        """Pills laid out left to right, wrapping at ``w``; returns the height used."""
        h = self.k["pill"] + 10
        cx, cy = x, y
        for s in items:
            pw = self.pill_width(s)
            if cx > x and cx + pw > x + w:
                cx, cy = x, cy + h + gap
            if pw > w:
                self.problem(f"chip {s!r} is wider than its row")
            self.pill(cx, cy, s, tone=tone, solid=solid, on=on)
            cx += pw + gap
        return cy + h - y

    def card(
        self,
        x: float,
        y: float,
        w: float,
        title: str,
        lines: Iterable[str] = (),
        tone: str = "slate",
        h: float | None = None,
        sub: str = "",
        mono: bool = False,
        tag: str = "",
        accent: bool = False,
        dashed: bool = False,
        center: bool = False,
    ) -> Box:
        """A card: a coloured stripe on the left, a title, an optional sub-title and body lines.

        ``h`` fixes the height (a problem is reported if the content does not fit); otherwise it fits the content.
        ``accent`` fills the card with the tone's tint; ``dashed`` marks something optional or not fitted.
        ``tag`` adds a pill in the top-right corner.
        """
        k = self.k
        pad = k["pad"]
        fill = f"{tone}.tint" if accent else "card"
        inner_w = w - 2 * pad - 6
        tag_w = self.pill_width(tag) + 8 if tag else 0
        t_lines = wrap(title, inner_w - tag_w, k["card_title"], 700)
        for t in t_lines:
            if text_width(t, k["card_title"], 700) > inner_w - tag_w + 0.5:
                self.problem(f"card title {t!r} is wider than its card")
        s_lines = wrap(sub, inner_w, k["small"]) if sub else []
        body = []
        for ln in lines:
            body += wrap(ln, inner_w, k["mono"] if mono else k["card_body"], 400, mono)
        th, sh, bh = k["card_title"] * 1.3, k["small"] * 1.35, (k["mono"] if mono else k["card_body"]) * 1.45
        need = pad + len(t_lines) * th + (len(s_lines) * sh + 2 if s_lines else 0) + (6 + len(body) * bh if body else 0)
        need += pad - 4
        hh = h if h is not None else math.ceil(need)
        if h is not None and need > h + 0.5:
            self.problem(f"card {title!r}: content needs {need:.0f} px but the card is {h:.0f} px tall")
        box = self.rect(x, y, w, hh, fill=fill, stroke=f"{tone}.strong" if accent else "line",
                        sw=1.4 if accent else 1.0, dash="6 4" if dashed else None)
        if not dashed:
            self._put("node", f'<rect x="{_f(self._X(x))}" y="{_f(self._Y(y) + 8)}" width="4" '
                              f'height="{_f(max(hh - 16, 4))}" rx="2" fill="{C(tone + ".strong")}"/>')
        tx = x + pad + 6
        anchor = "start"
        if center:
            tx, anchor = x + w / 2 + 3, "middle"
        if center and not (s_lines or body):
            cy = y + (hh - len(t_lines) * th) / 2 + k["card_title"] * 0.95
        else:
            cy = y + pad + k["card_title"] * 0.85
        for t in t_lines:
            self.text(tx, cy, t, size=k["card_title"], weight=700, color=f"{tone}.text" if accent else "ink", bg=fill,
                      anchor=anchor)
            cy += th
        for s in s_lines:
            self.text(tx, cy - th + sh + 1, s, size=k["small"], color=f"{tone}.text" if accent else "muted", bg=fill,
                      anchor=anchor)
            cy += sh
        if body:
            cy += 4
            for b in body:
                self.text(tx, cy - th + bh + 2, b, size=k["mono"] if mono else k["card_body"], color="ink2", bg=fill,
                          mono=mono, anchor=anchor)
                cy += bh
        if tag:
            self.pill(x + w - pad - self.pill_width(tag) + 4, y + pad - 4, tag, tone=tone, on=fill)
        for ob, other in self._cards:
            if box.overlaps(ob) and not (box.inside(ob) or ob.inside(box)):
                self.problem(f"card {title!r} overlaps card {other!r}")
        self._cards.append((box, title))
        return box

    def note(self, x: float, y: float, w: float, s: str, tone: str = "slate", title: str = "") -> Box:
        """A tinted call-out with wrapped text."""
        k = self.k
        size = k["note"]
        lines = wrap(s, w - 2 * k["pad"], size)
        th = (k["card_title"] * 1.3) if title else 0
        h = k["pad"] * 2 + th + len(lines) * size * 1.45 - size * 0.45
        self.rect(x, y, w, h, fill=f"{tone}.tint", stroke=f"{tone}.soft", sw=1.0)
        cy = y + k["pad"] + size * 0.85
        if title:
            self.text(x + k["pad"], y + k["pad"] + k["card_title"] * 0.85, title, size=k["card_title"], weight=700,
                      color=f"{tone}.text", bg=f"{tone}.tint")
            cy += th
        for ln in lines:
            self.text(x + k["pad"], cy, ln, size=size, color=f"{tone}.text", bg=f"{tone}.tint")
            cy += size * 1.45
        return Box(x, y, w, h)

    def step(self, x: float, y: float, n: int | str, tone: str = "brand") -> Box:
        """A numbered disc centred on (x, y)."""
        size = self.k["pill"]
        r = size * 0.95
        self._put("label", f'<circle cx="{_f(self._X(x))}" cy="{_f(self._Y(y))}" r="{_f(r)}" '
                           f'fill="{C(tone + ".text")}"/>')
        self.text(x, y + size * 0.36, str(n), size=size, weight=700, color="on", bg=f"{tone}.text", anchor="middle")
        self._grow(y + r)
        return Box(x - r, y - r, 2 * r, 2 * r)

    # ------------------------------------------------------------------ edges

    def _marker(self, tone_token: str) -> str:
        mid = "fk-arrow-" + tone_token.replace(".", "-")
        self._markers.add(tone_token)
        return mid

    def edge(
        self,
        pts: Sequence[tuple[float, float]],
        tone: str = "rail",
        label: str = "",
        dashed: bool = False,
        arrow: str = "end",
        label_at: float = 0.5,
        label_dx: float = 0.0,
        label_dy: float = 0.0,
        sw: float | None = None,
    ) -> None:
        """A polyline with rounded corners and arrowheads. ``tone`` is a tone name or a base token."""
        color = tone if tone in T.BASE else f"{tone}.strong"
        sw = sw or self.k["stroke"]
        d = _rounded([(self._X(px), self._Y(py)) for px, py in pts], 8)
        mk = self._marker(color)
        ms = f' marker-end="url(#{mk})"' if arrow in ("end", "both") else ""
        if arrow == "both":
            ms += f' marker-start="url(#{mk})"'
        da = ' stroke-dasharray="7 5"' if dashed else ""
        self._put("edge", f'<path d="{d}" fill="none" stroke="{C(color)}" stroke-width="{_f(sw)}" '
                          f'stroke-linecap="round" stroke-linejoin="round"{da}{ms}/>')
        for _px, py in pts:
            self._grow(py)
        self._segments += list(zip(pts, pts[1:]))
        if label:
            # Label on the longest segment around label_at.
            segs = list(zip(pts, pts[1:]))
            lens = [abs(b[0] - a[0]) + abs(b[1] - a[1]) for a, b in segs]
            total = sum(lens) or 1
            target, acc = total * label_at, 0.0
            for (a, b), ln in zip(segs, lens):
                if acc + ln >= target:
                    t = (target - acc) / (ln or 1)
                    lx, ly = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
                    break
                acc += ln
            self.edge_label(lx + label_dx, ly + label_dy, label)

    def edge_label(self, cx: float, cy: float, s: str, tone: str | None = None) -> Box:
        size = self.k["edge_label"]
        w = text_width(s, size, 500) + 12
        h = size + 8
        fill = f"{tone}.tint" if tone else "canvas"
        self.rect(cx - w / 2, cy - h / 2, w, h, fill=fill, stroke=None, rx=h / 2, layer="label")
        self.text(cx, cy + size * 0.36, s, size=size, weight=500, color=f"{tone}.text" if tone else "muted", bg=fill,
                  anchor="middle")
        box = Box(cx - w / 2, cy - h / 2, w, h)
        self._labels.append((box, s))
        return box

    def connect(
        self,
        a: Box,
        b: Box,
        a_side: str = "bottom",
        b_side: str = "top",
        at: float = 0.5,
        bt: float = 0.5,
        gap: float = 4.0,
        **kw,
    ) -> None:
        """Connect two boxes with an orthogonal route from a side of ``a`` to a side of ``b``."""
        p, q = list(a.port(a_side, at)), list(b.port(b_side, bt))
        off = {"bottom": (0, gap), "top": (0, -gap), "left": (-gap, 0), "right": (gap, 0)}
        p[0] += off[a_side][0]
        p[1] += off[a_side][1]
        q[0] += off[b_side][0]
        q[1] += off[b_side][1]
        vert_a, vert_b = a_side in ("top", "bottom"), b_side in ("top", "bottom")
        if vert_a and vert_b:
            if abs(p[0] - q[0]) < 0.5:
                pts = [tuple(p), tuple(q)]
            else:
                my = (p[1] + q[1]) / 2
                pts = [tuple(p), (p[0], my), (q[0], my), tuple(q)]
        elif not vert_a and not vert_b:
            if abs(p[1] - q[1]) < 0.5:
                pts = [tuple(p), tuple(q)]
            else:
                mx = (p[0] + q[0]) / 2
                pts = [tuple(p), (mx, p[1]), (mx, q[1]), tuple(q)]
        elif vert_a:
            pts = [tuple(p), (p[0], q[1]), tuple(q)]
        else:
            pts = [tuple(p), (q[0], p[1]), tuple(q)]
        self.edge(pts, **kw)

    # ------------------------------------------------------------------ checks and output

    def problems(self) -> list[str]:
        self._finalize()
        out = list(self._problems)
        if not self.alt or not self.alt.strip():
            out.append("no alt text")
        seen = set()
        for bb, fg, bg, s, _size in self._texts:
            for th in T.THEMES:
                ratio = T.contrast(T.resolve(fg, th), T.resolve(bg, th))
                key = (fg, bg, th)
                if ratio < MIN_CONTRAST and key not in seen:
                    seen.add(key)
                    out.append(f"{th}: text {s!r} is {ratio:.2f}:1 ({fg} on {bg}), needs {MIN_CONTRAST}:1")
            if bb.r > self.body_w + 0.5 and bb.x >= 0:
                out.append(f"text {s!r} runs past the right edge")
        if self.profile == "overview" and self.min_display_px < 11:
            out.append(f"overview figure: smallest text is {self.min_display_px} px at README width (needs 11)")
        return out

    def contrast_report(self) -> dict[str, float]:
        return {
            th: round(min((T.contrast(T.resolve(fg, th), T.resolve(bg, th)) for _b, fg, bg, _s, _z in self._texts),
                          default=21.0), 2)
            for th in T.THEMES
        }

    def _finalize(self) -> None:
        """Draw the legend under the body, once."""
        if getattr(self, "_end_y", None) is not None:
            return
        y = self._maxy
        if self.legend:
            size = self.k["legend"]
            x, row_y = 0.0, y + 26
            for tone, label in self.legend:
                w = 14 + 8 + text_width(label, size) + 24
                if x + w > self.body_w:
                    x, row_y = 0.0, row_y + size * 2
                self._put("label", f'<rect x="{_f(self._X(x))}" y="{_f(self._Y(row_y - size * 0.85))}" width="14" '
                                   f'height="14" rx="4" fill="{C(tone + ".tint")}" stroke="{C(tone + ".strong")}" '
                                   f'stroke-width="1.4"/>')
                self.text(x + 22, row_y + 1, label, size=size, color="muted", bg="canvas")
                x += w
        self._end_y = self._maxy
        # Arrows must not run under a card, and edge labels must not sit on one.
        for (x0, y0), (x1, y1) in self._segments:
            n = max(int(math.hypot(x1 - x0, y1 - y0) / 2), 1)
            for box, title in self._cards:
                inner = Box(box.x + 3, box.y + 3, box.w - 6, box.h - 6)
                hit = False
                for i in range(n + 1):
                    px, py = x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n
                    if inner.x < px < inner.r and inner.y < py < inner.b:
                        hit = True
                        break
                if hit:
                    self._problems.append(f"an arrow runs under card {title!r}")
        for (x0, y0), (x1, y1) in self._segments:
            n = max(int(math.hypot(x1 - x0, y1 - y0) / 2), 1)
            for bb, _fg, _bg, s, _size in self._texts:
                if any(bb.inside(lb) for lb, _s in self._labels):
                    continue
                for i in range(n + 1):
                    px, py = x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n
                    if bb.x + 1 < px < bb.r - 1 and bb.y + 1 < py < bb.b - 1:
                        self._problems.append(f"an arrow crosses the text {s!r}")
                        break
        for lb, s in self._labels:
            for box, title in self._cards:
                if lb.overlaps(box):
                    self._problems.append(f"edge label {s!r} sits on card {title!r}")

    def svg(self, theme: str = "light") -> str:
        self._finalize()
        height = math.ceil(self.oy + self._end_y + 30)
        k = self.k
        head = []
        if self.kicker:
            head.append(f'<text x="{self.pad}" y="{_f(34 + k["kicker"] * 0.8)}" font-size="{_f(k["kicker"])}" '
                        f'font-weight="700" letter-spacing="1.2" fill="{C("brand.text")}">{escape(self.kicker.upper())}'
                        f'</text>')
        head.append(f'<text x="{self.pad}" y="{_f(self._title_y)}" font-size="{_f(k["title"])}" font-weight="700" '
                    f'fill="{C("ink")}">{escape(self.title)}</text>')
        for i, s in enumerate(self._sub_lines):
            head.append(f'<text x="{self.pad}" y="{_f(self._sub_y + i * k["subtitle"] * 1.45)}" '
                        f'font-size="{_f(k["subtitle"])}" fill="{C("muted")}">{escape(s)}</text>')
        head.append(f'<rect x="{self.pad}" y="{_f(self.oy - 18)}" width="{self.body_w}" height="1" '
                    f'fill="{C("hair")}"/>')
        defs = []
        for tok in sorted(self._markers):
            mid = "fk-arrow-" + tok.replace(".", "-")
            defs.append(f'<marker id="{mid}" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" '
                        f'orient="auto-start-reverse"><path d="M0 0L10 5L0 10z" fill="{C(tok)}"/></marker>')
        tid, did = f"{self.fid}-title", f"{self.fid}-desc"
        body = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" height="{height}" '
            f'viewBox="0 0 {self.width} {height}" role="img" aria-labelledby="{tid} {did}" '
            f'font-family="{SANS}">',
            f'<title id="{tid}">{escape(self.title)}</title>',
            f'<desc id="{did}">{escape(self.alt)}</desc>',
            f'<defs>{"".join(defs)}</defs>' if defs else "",
            f'<rect x="0.5" y="0.5" width="{self.width - 1}" height="{height - 1}" rx="16" fill="{C("canvas")}" '
            f'stroke="{C("frame")}"/>',
            *head,
        ]
        for layer in LAYERS:
            body += self._layers[layer]
        body.append("</svg>")
        out = "\n".join(s for s in body if s) + "\n"
        return _resolve(out, theme)


def _resolve(svg: str, theme: str) -> str:
    parts = svg.split("§")
    for i in range(1, len(parts), 2):
        parts[i] = T.resolve(parts[i], theme)
    return "".join(parts)


def _rounded(pts: Sequence[tuple[float, float]], radius: float) -> str:
    if len(pts) < 3:
        return " ".join(f"{'M' if i == 0 else 'L'}{_f(x)} {_f(y)}" for i, (x, y) in enumerate(pts))
    d = [f"M{_f(pts[0][0])} {_f(pts[0][1])}"]
    for i in range(1, len(pts) - 1):
        (x0, y0), (x1, y1), (x2, y2) = pts[i - 1], pts[i], pts[i + 1]
        l1 = math.hypot(x1 - x0, y1 - y0)
        l2 = math.hypot(x2 - x1, y2 - y1)
        r = min(radius, l1 / 2, l2 / 2)
        if r < 0.5:
            d.append(f"L{_f(x1)} {_f(y1)}")
            continue
        ax, ay = x1 - (x1 - x0) / l1 * r, y1 - (y1 - y0) / l1 * r
        bx, by = x1 + (x2 - x1) / l2 * r, y1 + (y2 - y1) / l2 * r
        d.append(f"L{_f(ax)} {_f(ay)} Q{_f(x1)} {_f(y1)} {_f(bx)} {_f(by)}")
    d.append(f"L{_f(pts[-1][0])} {_f(pts[-1][1])}")
    return " ".join(d)
