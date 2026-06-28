# PocketPage

Portable e-reader for those on the go!

## Status

**Hardware bring-up complete (2026-06-27).** The panel is wired, soldered, and
driving correctly. The current sketch is a speed/diagnostic demo (a bouncing box
plus a live fps counter) that proves the display path end-to-end — real partial
and full refreshes confirmed. The actual e-reader application is the next step.

## Hardware

| Part   | Detail |
| ------ | ------ |
| MCU    | ELEGOO ESP32 (ESP32-WROOM-32), CP2102/CH340 USB-UART bridge |
| Panel  | Waveshare 2.13" e-Paper HAT V4 (GoodDisplay GDEY0213B74 / SSD1680, 122 × 250) |
| Driver | GxEPD2 — `GxEPD2_213_B74` class, black & white, fast partial update |

The 8-wire HAT cable is **permanently soldered** to the ESP32 pin tails. Leave the
HAT's `BS` interface jumper at `0` (4-line SPI, the factory default).

## Wiring

Waveshare HAT pad → ESP32 pin (hardware VSPI; MISO unused on this write-only panel):

| HAT label | ESP32 pin | GPIO |
| --------- | --------- | ---- |
| VCC       | 3V3       | —    |
| GND       | GND       | —    |
| DIN       | D23       | 23   |
| CLK       | D18       | 18   |
| CS        | D5        | 5    |
| DC        | TX2       | 17   |
| RST       | RX2       | 16   |
| BUSY      | D4        | 4    |

## Build & flash

Requires [`arduino-cli`](https://arduino.github.io/arduino-cli/) with the
`esp32:esp32` core and the `GxEPD2` library installed.

```sh
make build     # compile only (no board needed)
make upload    # compile + flash
make monitor   # open the serial monitor @ 115200
make           # upload then monitor (default)
```

Default port is `/dev/ttyUSB0`. Override it if needed:

```sh
make upload PORT=/dev/ttyUSB1
```

If a flash ever fails: hold **BOOT**, tap **RESET**, release **BOOT**, then retry.
