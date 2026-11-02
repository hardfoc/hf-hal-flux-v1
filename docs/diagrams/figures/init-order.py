"""The order EnsureInitialized() brings the managers up, and which failures stop it.

Reads: Left to right, then the second row. The facade initialises the managers one at a time, in the order of the
calls in its init function. The first group is required: if any of them fails, EnsureInitialized() returns false
and the application should not continue. The second group is best effort: a failure there is logged as a warning,
the manager stays unavailable and the system reports itself as degraded, so a bench with only some chips fitted
still comes up. The order and the required / best-effort split are read from the init function, which returns
false only in the required branches.
"""

import hal_extract as hx
from figkit import Fig


def data():
    f = hx.facade()
    return {"facade": hx.BOARD["facade_class"], "init": f["init"], "sources": f["sources"][1:]}


def build(data):
    steps = data["init"]
    req = [s for s in steps if s["required"]]
    opt = [s for s in steps if not s["required"]]
    fig = Fig(
        "init-order",
        f"{data['facade']}::EnsureInitialized()",
        alt=f"Initialisation order: {', '.join(s['class'] for s in req)} are required; then "
        f"{', '.join(s['class'] for s in opt)} are best effort and only produce warnings if they fail.",
        width=1200,
        kicker="Start-up sequence",
        subtitle="Managers come up one at a time, in this order. A required step that fails stops initialisation; "
        "a best-effort step that fails leaves the system running, degraded.",
        legend=[("brand", "required: failure returns false"), ("slate", "best effort: failure logs a warning")],
    )
    W = fig.body_w
    cols = 4
    cw, gap = (W - 48 - (cols - 1) * 24) / cols, 24
    rows = [req, opt]
    y = 0
    prev_box = None
    n = 0
    for gi, group in enumerate(rows):
        tone = "brand" if gi == 0 else "slate"
        label = "Required" if gi == 0 else "Best effort"
        per_row = cols
        nrows = (len(group) + per_row - 1) // per_row
        top = fig.region_inner_top(y)
        card_h = 92
        h = (top - y) + nrows * card_h + (nrows - 1) * 24 + 20
        fig.region(0, y, W, h, label, tone=tone)
        for i, s in enumerate(group):
            r, c = divmod(i, per_row)
            x = 24 + c * (cw + gap)
            cy = top + r * (card_h + 24)
            n += 1
            box = fig.card(x, cy, cw, s["class"], [f"{data['facade'].lower()}.{s['member']}"], tone=tone, h=card_h,
                           mono=True, accent=gi == 0, dashed=gi == 1)
            fig.step(x + cw - 22, cy + 22, n, tone=tone)
            if prev_box is not None:
                if abs(prev_box.cy - box.cy) < 1:
                    fig.connect(prev_box, box, "right", "left", tone=tone)
                else:
                    fig.connect(prev_box, box, "bottom", "top", bt=0.6, tone="rail")
            prev_box = box
        y += h + 28
    return fig
