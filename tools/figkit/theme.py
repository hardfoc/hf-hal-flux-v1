"""Colour tokens for figkit figures. Every colour has a light and a dark value.

A figure never writes a hex value. It names a token: a base token (``"ink"``, ``"card"``) or a tone part
(``"blue.tint"``, ``"amber.strong"``). The figure is laid out once and ``Fig.svg(theme)`` resolves the tokens for the
light or the dark theme, so the two renderings cannot drift apart.

A tone has four parts:

* ``strong`` - stripes, arrows, icons and the outline of an accented card;
* ``text``   - text written on the tone's tint;
* ``tint``   - fill of a region or an accented card;
* ``soft``   - outline of a region or a chip.

Every text / background pair used by a figure is checked against WCAG 2.x (at least 4.5:1) in both themes when the
figure is built, so a palette change that breaks contrast fails the build.
"""

from __future__ import annotations

#: Base tokens: name -> (light, dark).
BASE: dict[str, tuple[str, str]] = {
    "canvas": ("#FFFFFF", "#0E131A"),  # figure background
    "frame": ("#E2E7EE", "#283241"),  # figure outline
    "card": ("#FFFFFF", "#151C26"),  # card fill
    "paper": ("#F5F7FA", "#18202B"),  # quiet band
    "ink": ("#17202C", "#E8EDF3"),  # primary text
    "ink2": ("#2B3442", "#D2DAE4"),  # body text
    "muted": ("#566070", "#9AA6B5"),  # secondary text (still >= 4.5:1 on card and paper)
    "line": ("#C9D1DB", "#3A4657"),  # card outline
    "hair": ("#E2E7EE", "#252F3C"),  # rules
    "rail": ("#8893A2", "#6C7889"),  # neutral arrows (never text)
    "on": ("#FFFFFF", "#0E131A"),  # text on a solid pill
}

#: Tones: name -> {"light": (strong, text, tint, soft), "dark": (strong, text, tint, soft)}.
TONES: dict[str, dict[str, tuple[str, str, str, str]]] = {
    "brand": {"light": ("#4F46E5", "#3730A3", "#EEF0FF", "#C7CBF8"), "dark": ("#8C93FF", "#C7CBFF", "#1C1F3D", "#3B4080")},
    "blue": {"light": ("#2563EB", "#1E429F", "#EAF2FF", "#BCD2F9"), "dark": ("#60A5FA", "#BFDBFE", "#13233A", "#2B4C78")},
    "teal": {"light": ("#0F8B80", "#115E59", "#E6F6F4", "#A6DCD5"), "dark": ("#2DD4BF", "#A7F3E6", "#0F2A28", "#215A54")},
    "violet": {"light": ("#7C3AED", "#5B21B6", "#F3EEFF", "#D5C5FA"), "dark": ("#A78BFA", "#DDD6FE", "#221A3A", "#4B3A82")},
    "amber": {"light": ("#C26A05", "#8A3C0B", "#FFF5E3", "#F1CF93"), "dark": ("#FBBF24", "#FDE68A", "#2C2310", "#6A5220")},
    "rose": {"light": ("#DB2752", "#9F1239", "#FFEEF2", "#F5BECB"), "dark": ("#FB7185", "#FECDD3", "#321820", "#6E3141")},
    "green": {"light": ("#15803D", "#14532D", "#EAF7EF", "#B2E2C2"), "dark": ("#4ADE80", "#BBF7D0", "#11281A", "#285A3A")},
    "slate": {"light": ("#64748B", "#334155", "#F1F4F8", "#CBD5E1"), "dark": ("#94A3B8", "#E2E8F0", "#1B232E", "#3A4656")},
}

PARTS = ("strong", "text", "tint", "soft")
THEMES = ("light", "dark")


def resolve(token: str, theme: str) -> str:
    """Hex value of ``token`` in ``theme``; raises KeyError for an unknown token."""
    i = THEMES.index(theme)
    if token in BASE:
        return BASE[token][i]
    tone, _, part = token.partition(".")
    if tone in TONES and part in PARTS:
        return TONES[tone][theme][PARTS.index(part)]
    raise KeyError(f"unknown colour token {token!r}")


def _lin(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(hex_colour: str) -> float:
    h = hex_colour.lstrip("#")
    r, g, b = (int(h[i : i + 2], 16) / 255 for i in (0, 2, 4))
    return 0.2126 * _lin(r) + 0.7152 * _lin(g) + 0.0722 * _lin(b)


def contrast(fg: str, bg: str) -> float:
    """WCAG 2.x contrast ratio between two hex colours."""
    a, b = sorted((luminance(fg), luminance(bg)), reverse=True)
    return (a + 0.05) / (b + 0.05)
