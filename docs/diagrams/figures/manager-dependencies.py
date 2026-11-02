"""Which hf-core handler and which chip driver each manager uses, from the #include lines.

Reads: Managers on the left, hf-core handlers in the middle, chip drivers on the right. An arrow means "includes and
uses": a manager owns the handler it points at, and each handler wraps exactly one driver. ValveManager owns both
valve drivers (MAX22200 for the solenoid channels, TLE92466ED for the proportional channels), AdcManager owns the
ADS7952 external ADC, and TemperatureManager owns the NTC and MCP9700 sensor handlers. LedManager uses its driver
directly, without a handler. Managers with no arrow (communication buses, GPIO, NVS) talk to the interface wrap
only.
"""

import hal_extract as hx
from hardfoc_mkdocs_theme.figkit import Fig


def data():
    d = hx.dependencies()
    return {
        "managers": {k: {"handlers": v["handlers"], "drivers": v["drivers"]} for k, v in d["managers"].items()},
        "handler_drivers": d["handler_drivers"],
        "sources": d["sources"],
    }


def build(data):
    mgrs = data["managers"]
    hd = data["handler_drivers"]
    handlers = sorted(hd)
    drivers = sorted({x for v in hd.values() for x in v} | {x for m in mgrs.values() for x in m["drivers"]})
    fig = Fig(
        "manager-dependencies",
        "Managers, handlers and drivers",
        alt="Graph from each manager to the hf-core handlers it uses and from each handler to the chip driver it wraps: "
        "ValveManager to the MAX22200 and TLE92466ED handlers, AdcManager to the ADS7952 handler, TemperatureManager to "
        "the NTC and MCP9700 handlers, and LedManager directly to the WS2812 driver.",
        width=1200,
        kicker="Architecture",
        subtitle="An arrow means the left side includes and uses the right side. Read from the #include lines of "
        "lib/managers and lib/core/handlers.",
        legend=[("blue", "manager (lib/managers)"), ("teal", "hf-core handler"), ("violet", "chip driver")],
    )
    W = fig.body_w
    cw, hw, dw = 250, 290, 260
    xm, xh, xd = 0, (W - hw) / 2, W - dw
    rh, rg = 46, 14
    order = sorted(mgrs, key=lambda m: (not mgrs[m]["handlers"] and not mgrs[m]["drivers"], m))
    mbox = {}
    for i, m in enumerate(order):
        mbox[m] = fig.card(xm, i * (rh + rg), cw, m, tone="blue", h=rh)
    total_h = len(order) * (rh + rg) - rg
    # Handlers: spread evenly over the height, in the order of their first user.
    first = {h: min(i for i, m in enumerate(order) if h in mgrs[m]["handlers"]) for h in handlers}
    hs = sorted(handlers, key=lambda h: first[h])
    hstep = (total_h - rh) / max(len(hs) - 1, 1) if len(hs) > 1 else 0
    hbox = {}
    for i, h in enumerate(hs):
        cls = next((p.stem for p in (hx.ROOT / hx.BOARD["handlers_dir"] / h).glob("*Handler.h")), h)
        hbox[h] = fig.card(xh, i * hstep * 0.78, hw, cls, tone="teal", h=rh)
    dbox = {}
    for d in drivers:
        users = [hbox[h].cy for h in hs if d in hd[h]]
        dbox[d] = users[0] if users else total_h  # drivers used without a handler go last
    placed = []
    for d in sorted(drivers, key=lambda k: dbox[k]):
        y = max(dbox[d] - rh / 2, (placed[-1] + rh + rg) if placed else 0)
        placed.append(y)
        dbox[d] = fig.card(xd, y, dw, f"{d}-driver", tone="violet", h=rh)
    # Edges: manager -> handler, fanning into the handler's left side.
    # Each handler gets its own vertical lane, so arrows into different handlers never share a line.
    lane0, lane_gap = cw + 28, 22
    for i, h in enumerate(hs):
        users = [m for m in order if h in mgrs[m]["handlers"]]
        lx = lane0 + i * lane_gap
        for k, m in enumerate(users):
            t = (k + 1) / (len(users) + 1)
            ty = hbox[h].y + hbox[h].h * t
            fig.edge([(mbox[m].r + 4, mbox[m].cy), (lx, mbox[m].cy), (lx, ty), (xh - 4, ty)], tone="blue")
    for h in hs:
        for d in hd[h]:
            fig.connect(hbox[h], dbox[d], "right", "left", tone="teal")
    for m in order:
        for d in mgrs[m]["drivers"]:
            b = mbox[m]
            target = dbox[d]
            y0 = b.cy
            low = max(total_h, target.b) + 26
            lx = lane0 + len(hs) * lane_gap
            fig.edge([(b.r + 4, y0), (lx, y0), (lx, low), (xd - 40, low), (xd - 40, target.cy),
                      (target.x - 4, target.cy)], tone="violet", dashed=True)
            fig.edge_label((lx + xd - 40) / 2, low, f"{m} uses {d}-driver directly")
    return fig
