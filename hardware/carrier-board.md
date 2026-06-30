# PocketPage — Carrier Board (Rev A) design spec

Goal: replace the hand-wired rats-nest with one **carrier PCB**. The ESP32 dev
board, the microSD module, and a LiPo charger/boost module all **plug into female
headers** (socketed = swappable, no resoldering when a pin/board dies). The
carrier itself is just interconnect + 3 switches + connectors + decoupling caps.
This is the lowest-risk way to kill the wire failures while keeping everything
field-serviceable.

Philosophy: **the carrier holds NO active silicon you can't unplug.** If something
fails, swap the module. The board is copper traces + sockets + passives only.

---

## Netlist (verified working — same pinout as the firmware)

| Net | ESP32 pin | e-paper pad | microSD | Switches | Notes |
| --- | --- | --- | --- | --- | --- |
| `3V3`        | 3V3   | VCC  | 3V3  | —        | dev-board regulator output; bulk caps here |
| `GND`        | GND   | GND  | GND  | all legs | common ground plane |
| `5V`         | VIN   | —    | —    | —        | from battery module, via power switch |
| `SPI_CLK`    | 18    | CLK  | CLK  | —        | **shared** panel + SD |
| `SPI_MOSI`   | 13    | DIN  | MOSI | —        | **shared**; GPIO13 (GPIO23 driver is dead) |
| `SPI_MISO`   | 19    | —    | MISO | —        | SD read-back only |
| `EPD_CS`     | 5     | CS   | —    | —        | |
| `EPD_DC`     | 17    | DC   | —    | —        | |
| `EPD_RST`    | 16    | RST  | —    | —        | |
| `EPD_BUSY`   | 4     | BUSY | —    | —        | |
| `SD_CS`      | 21    | —    | CS   | —        | |
| `BTN_NEXT`   | 25    | —    | —    | SW1      | other leg → GND |
| `BTN_MENU`   | 26    | —    | —    | SW2      | other leg → GND |
| `BTN_LAST`   | 27    | —    | —    | SW3      | other leg → GND |

**GPIO23: leave a no-connect.** This board's GPIO23 output driver is damaged.
The firmware already uses GPIO13 for DIN/MOSI — keep it. (If you ever socket a
*fresh* ELEGOO board you could move back to GPIO23, but there's no reason to;
GPIO13 is proven and keeps the firmware unchanged.)

---

## Power subsystem (the portable part)

```
USB-C ──► [LiPo charger + 5V boost module] ──► power switch ──► ESP32 VIN (5V)
              │            ▲
              └─ charges ──┴── single-cell LiPo (JST-PH 2-pin)
```

- **Why boost to 5V into VIN, not feed the battery to 3V3 directly?** A LiPo is
  3.0–4.2 V. At 4.2 V it would **over-volt the SD card** (3.6 V max) and at 3.0 V
  it can't cleanly feed the 3.3 V parts. So: boost the cell to a stable 5 V, let
  the dev board's regulator make clean 3.3 V for everything. Slightly less
  efficient, but safe and simple — right call for "basic battery life."
- **Recommended module:** an integrated **IP5306-based power-bank module**
  (USB-C in, charges the cell, outputs regulated 5 V, handles load-while-charging,
  has the charge LEDs). One module = charger + boost + power-path. Socket it.
- **Discrete alternative:** TP4056 (charge) + MT3608 (boost to 5 V). More parts,
  more control.
- **Power switch:** a slide switch on the boost **output** (before VIN) for
  on/off. When reflashing, plug USB into the dev board as usual.

> **Stage-2 caveat (deep sleep):** IP5306 modules **auto-shut-off** under the µA
> load of ESP32 deep sleep — fine now (we're not deep-sleeping yet), but if you
> later chase weeks-per-charge, you'll need a different power topology. Noted so
> it doesn't bite you.

---

## Decoupling — fixes the brownouts you already hit

You saw real brownout resets from the e-paper boost inrush and worried about SD
spikes. Put these on the carrier so they're permanent:

| Cap | Location | Why |
| --- | --- | --- |
| 100 µF | across `3V3`/`GND`, bulk | rail stability |
| 100 µF | at the **e-paper VCC** pad | panel boost-converter inrush |
| 10 µF  | at the **SD 3V3** pad | SD card access spikes |
| 100 nF | near each module's power pins | standard HF decoupling |

---

## Connectors & components (BOM, Rev A)

| Ref | Part | Notes |
| --- | --- | --- |
| U1 | 2× 19-pin **female** headers (2.54 mm) | sockets the ELEGOO ESP32 dev board |
| U2 | 6-pin female header | sockets the microSD module |
| U3 | IP5306 power-bank module + pads/header | charger + 5 V boost |
| BT1 | single-cell LiPo, **JST-PH 2-pin** | 1000–2000 mAh |
| SW1–3 | **Kailh Choc v1 + hotswap sockets** | page/menu keys — see `switches-choc.md` |
| SW4 | slide switch | battery power on/off |
| J1 | 8-pin connector (2.54 mm or JST-XH) | e-paper, use the **spare HAT's intact cable** |
| C1–C4 | caps per table above | |

**E-paper connection:** your first HAT's cable got cut/soldered, but you have a
**2-pack** — use the **second HAT and its intact 8-pin cable** into J1. Pin order
on J1 matches the HAT silk: `VCC GND DIN CLK CS DC RST BUSY`. (Alternative: a 2×20
socket to plug the HAT in like a shield — solid but bulkier; the 8-pin cable gives
you freedom to position the panel for the handheld form factor.)

---

## Layout notes

- **Antenna keepout:** the dev board's antenna overhangs one end — keep the LiPo
  and any copper pour out from under it.
- **Switches** SW1–3 in a vertical column along the panel's long edge (your
  intended ergonomics).
- **Ground pour** both sides, stitched, for a clean shared-SPI return.
- **Mounting holes:** 4× for the enclosure + panel standoffs.
- Traces are short here, so signal integrity is a non-issue — but you *could*
  later raise the SD clock above the conservative 1 MHz the firmware uses now.

---

## Tooling / order flow

1. **KiCad** (free) — schematic from the netlist above, then PCB layout.
2. Export Gerbers → **JLCPCB / PCBWay** (~$5 for 5 boards + ~2 wk shipping).
3. Hand-solder: it's just headers, switches, connectors, and 4 caps — all
   through-hole / large pads. No fine-pitch SMD required.

## Staged plan

- **Stage 1 (this doc):** carrier board — kills the rats-nest + fragility, adds
  basic portability. Firmware unchanged.
- **Stage 2 (optional, later):** bare WROOM module + deep-sleep firmware + a
  low-power-friendly charger for weeks-per-charge, plus a slim enclosure.
