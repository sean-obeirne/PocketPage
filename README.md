# PocketPage

Portable e-reader for those on the go!

## Status

**E-reader firmware (2026-06-27).** Reads `.txt` files from a microSD card and
paginates them on the e-paper, navigated by three Cherry MX keys. The display,
SD card, and buttons are all wired and the firmware builds clean. Text layout is
streamed from the file (not loaded into RAM), so books of any size work.

## Controls

Three Cherry MX keys run down the long edge of the display:

| Key  | While reading      | In the library menu      |
| ---- | ------------------ | ------------------------ |
| NEXT | Next page          | Move selection down      |
| MENU | Open the library   | Open the highlighted book |
| LAST | Previous page      | Move selection up        |

## Hardware

| Part    | Detail |
| ------- | ------ |
| MCU     | ELEGOO ESP32 (ESP32-WROOM-32), CP2102/CH340 USB-UART bridge |
| Panel   | Waveshare 2.13" e-Paper HAT V4 (GoodDisplay GDEY0213B74 / SSD1680, 122 × 250) |
| Driver  | GxEPD2 — `GxEPD2_213_B74` class, black & white, fast partial update |
| Storage | microSD module on SPI (e.g. Samsung EVO Plus 64GB), FAT32, `.txt` files in root |
| Input   | 3× Cherry MX switches (NEXT / MENU / LAST), internal pull-ups |

The 8-wire HAT cable is **permanently soldered** to the ESP32 pin tails. Leave the
HAT's `BS` interface jumper at `0` (4-line SPI, the factory default). The SD card
shares the panel's SPI bus (CLK + MOSI), adding only its own MISO and chip-select.

## Wiring

### e-paper panel — Waveshare HAT pad → ESP32 pin (hardware VSPI)

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

### microSD module → ESP32 pin (shares CLK + MOSI with the panel)

| SD pad | ESP32 pin | GPIO |
| ------ | --------- | ---- |
| 3V3    | 3V3       | —    |
| GND    | GND       | —    |
| CLK    | D18       | 18   |
| MOSI   | D23       | 23   |
| MISO   | D19       | 19   |
| CS     | D21       | 21   |

### Cherry MX keys → ESP32 pin (one leg to GPIO, the other leg to GND)

| Key  | ESP32 pin | GPIO |
| ---- | --------- | ---- |
| NEXT | D25       | 25   |
| MENU | D26       | 26   |
| LAST | D27       | 27   |

The keys use the ESP32's internal pull-ups (idle HIGH, pressed pulls to GND), so
no external resistors are needed. GPIOs 25/26/27 are adjacent on the header for
easy routing down the display's long edge.

## Build & flash

Requires [`arduino-cli`](https://arduino.github.io/arduino-cli/) with the
`esp32:esp32` core and the `GxEPD2` library installed. Put one or more `.txt`
files in the root of a FAT32-formatted microSD card.

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
