# PocketPage — Input Switches (Kailh Choc)

Decision (2026-06-29): switch the 3 page/menu keys from **Cherry MX** to **Kailh
Choc** for a slimmer handheld. Electrically identical to the firmware (still
`leg -> GPIO, leg -> GND`, internal pull-ups) — this is a **footprint + mechanical**
change only. No code changes.

## Choice: Choc **v1** + **hotswap sockets**

| Aspect | Decision | Why |
| --- | --- | --- |
| Family | **Kailh Choc v1 (PG1350)** | lowest profile, widest low-profile keycap selection |
| Mount | **Hotswap sockets** (Kailh Choc) | snap-in switches, no switch soldering, swap a dead one instantly — matches the project's "socket everything" rule after the solder-joint saga |
| Footprint | **`PG1350` (Choc v1)** in KiCad | NOT MX-compatible; do not use the MX footprint |
| Keycaps | **Choc v1 stem** caps | MX keycaps will NOT fit Choc |

## Why Choc over MX here
- **Height:** Choc switch ~11.9 mm vs MX ~16.5 mm → ~2–3 mm thinner device with
  low-profile caps. Big deal for a pocket e-reader.
- **Spacing:** Choc 18×17 mm vs MX 19.05 mm → tighter button column along the
  panel's long edge.
- **Feel:** light actuation options suit repeated page turns.

## Footprint / ordering gotchas (decide BEFORE drawing the PCB)
1. **v1 (PG1350) ≠ v2 (PG1353).** Different footprints, NOT interchangeable.
   We picked **v1**. Order v1 switches to match the v1 footprint.
2. **Hotswap footprint differs from solder-in.** Use the **Choc hotswap** variant
   of the PG1350 footprint (it has the socket pads + the 2 plastic boss holes).
3. **Keycaps are Choc-specific.** Confirm v1 stem when buying caps. MX caps are
   incompatible.
4. **Boss/alignment holes:** Choc has 2 plastic legs — include those holes or the
   switch won't seat flat.

## Electrical (unchanged from carrier-board.md)
| Key | ESP32 GPIO | Wiring |
| --- | --- | --- |
| NEXT | 25 | leg -> GPIO25, other leg -> GND |
| MENU | 26 | leg -> GPIO26, other leg -> GND |
| LAST | 27 | leg -> GPIO27, other leg -> GND |

Internal pull-ups (idle HIGH, pressed LOW). No external resistors. All three
ground legs may chain to one GND.

## Order now (lead time, like the battery)
- 3× (or a few spare) **Kailh Choc v1** switches
- 3× **Kailh Choc hotswap sockets**
- 3× **Choc v1 keycaps**

## Enclosure note
The ~2–3 mm height saving vs MX flows into the case design — plan the front-panel
switch cutouts and overall thickness around the Choc dimensions, not MX.
