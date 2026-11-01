"""The Flux V1 board as the software sees it: the ESP32-S3, each bus, and the chips on it.

Reads: The ESP32-S3 on the left runs all the software. Each arrow to the right is one bus or signal, labelled with
the GPIOs it uses. One SPI bus is shared by the two valve drivers: the MAX22200 (eight solenoid channels) and the
TLE92466ED (six proportional, current-controlled channels), each with its own chip select and its own control and
fault lines. A second SPI bus is dedicated to the ADS7952 12-channel external ADC. The ESP32-S3's own ADC reads the
NTC thermistor and the bus-voltage divider. TWAI goes to the CAN transceiver, a UART is reserved for the FDO2 oxygen
probe, and one data line drives the WS2812 status LED. The layout is drawn by hand; every GPIO number, ADC channel,
chip select and SPI mode is read from the functional pin map and the comms manager, so a pin change rebuilds this
figure.
"""

import hal_extract as hx
from figkit import Fig


def data():
    p, b = hx.pins(), hx.buses()
    return {
        "gpio": {g["id"]: g for g in p["gpio"]},
        "adc": {a["id"]: a for a in p["adc"]},
        "spi": b["spi"],
        "sources": p["sources"] + b["sources"][:1],
    }


def build(data):
    g, a = data["gpio"], data["adc"]

    def io(pin_id: str) -> str:
        return f"GPIO{g[pin_id]['pin']}"  # KeyError here means the pin map changed: update the figure

    def ch(adc_id: str) -> str:
        return f"CH{a[adc_id]['channel']}"

    mode = {d["id"]: d["mode"] for d in data["spi"]}
    n_ads = sum(1 for x in a.values() if x["chip"] == "ADS7952_EXTERNAL")
    fig = Fig(
        "board-overview",
        "Flux V1: buses and chips",
        alt="Block diagram of the Flux V1 board: the ESP32-S3 connects over one SPI bus to the MAX22200 solenoid driver "
        "and the TLE92466ED proportional driver, over a second SPI bus to the ADS7952 ADC, over its internal ADC to the "
        "NTC thermistor and bus-voltage divider, over TWAI to the CAN transceiver, over UART to the FDO2 probe and to a "
        "WS2812 LED.",
        width=1200,
        kicker="Hardware",
        subtitle="Every GPIO, ADC channel and SPI mode below is read from the pin map and the comms manager.",
        legend=[("brand", "MCU"), ("blue", "valve drivers"), ("teal", "sensing"), ("green", "communication"),
                ("amber", "status")],
    )
    W = fig.body_w
    mcu = fig.card(0, 0, 230, "ESP32-S3", ["dual-core MCU", "runs the HAL and your app",
                                          f"USB-JTAG on {io('USB_D_N')}/{g['USB_D_P']['pin']}"],
                   tone="brand", accent=True, h=770)
    dx = 610
    dw = W - dx
    maxc = fig.card(dx, 0, dw, "MAX22200", ["8 solenoid channels (SOL_CH0-7)", "hit / hold current or duty"],
                    tone="blue", h=110)
    tle = fig.card(dx, 130, dw, "TLE92466ED", ["6 proportional channels (PROP_CH0-5)", "current control, PWM, dither"],
                   tone="blue", h=110)
    ads = fig.card(dx, 260, dw, "ADS7952", [f"{n_ads}-channel 12-bit external ADC", f"MCP9700 sensor on {ch('ADS7952_0_CH8')}"],
                   tone="teal", h=96)
    ntc = fig.card(dx, 376, dw, "NTC thermistor and VBUS divider",
                   [f"ESP32 ADC1 {ch('ESP32_NTC_THERMISTOR')} and {ch('ESP32_VBUS_MONITOR')} "
                    f"(divider {a['ESP32_VBUS_MONITOR']['divider']:g})"], tone="teal", h=86)
    can = fig.card(dx, 482, dw, "CAN transceiver", ["external devices"], tone="green", h=76)
    fdo = fig.card(dx, 578, dw, "FDO2 oxygen probe", ["UART, acquired on demand"], tone="green", h=76, dashed=True)
    led = fig.card(dx, 674, dw, "WS2812", ["RGB status LED"], tone="amber", h=76)

    def bus(target, y, label, tone, dashed=False):
        fig.edge([(mcu.r + 4, y), (target.x - 4, y)], tone=tone, label=label, label_at=0.5, dashed=dashed)

    fig.text(mcu.r + 18, 12, f"Valve SPI: SCK {io('SPI2_SCK')}, MOSI {io('SPI2_MOSI')}, MISO {io('SPI2_MISO')}",
             size=fig.k["small"], color="muted")
    bus(maxc, 36, f"SPI mode {mode['MAX22200_SOLENOID_DRIVER']}, CS {io('SPI2_CS_MAX22200')}", "blue")
    bus(maxc, 80, f"EN {io('MAX22200_ENABLE')}, CMD {io('MAX22200_CMD')}, FAULT {io('MAX22200_FAULT_N')}", "blue")
    bus(tle, 166, f"SPI mode {mode['TLE92466_LOWSIDE_DRIVER']}, CS {io('SPI2_CS_TLE92466')}", "blue")
    bus(tle, 210, f"EN {io('TLE92466_EN')}, RESN {io('TLE92466_RESN')}, FAULT {io('TLE92466_FAULT_N')}", "blue")
    bus(ads, 296, f"ADC SPI mode {mode['ADS7952_ADC_0']}, CS {io('SPI3_CS_ADS7952_0')}", "teal")
    bus(ads, 330, f"SCK {io('SPI3_SCK')}, MOSI {io('SPI3_MOSI')}, MISO {io('SPI3_MISO')}", "teal")
    bus(ntc, 419, "analog inputs", "teal")
    bus(can, 520, f"TWAI TX {io('TWAI_TX')} / RX {io('TWAI_RX')}", "green")
    bus(fdo, 616, f"UART TX {io('UART_FDO2_TX')} / RX {io('UART_FDO2_RX')}", "green", dashed=True)
    bus(led, 712, f"RMT data {io('WS2812_LED_DAT')}", "amber")
    fig.note(0, 800, W,
             f"MAX22200 trigger inputs: TRIGA {io('MAX22200_TRIGA')}, TRIGB {io('MAX22200_TRIGB')}. TLE92466ED direct "
             f"drive inputs: DRV0 {io('TLE92466_DRV0')}, DRV1 {io('TLE92466_DRV1')}. The pin map calls the valve bus "
             "SPI2 and the ADC bus SPI3; in ESP-IDF terms the valve bus runs on SPI3_HOST and the ADC bus on SPI2_HOST "
             "(see CommChannelsManager.cpp).",
             tone="slate", title="Also on the pin map")
    return fig
