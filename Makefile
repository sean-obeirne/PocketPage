# ============================================================================
#  Makefile — build / upload / monitor for the Cue e-paper sketch
#  Target board: ELEGOO ESP32 (ESP32-WROOM-32 DevKit, CP2102 USB-UART)
# ============================================================================
#  Common usage:
#     make            # compile + upload + monitor (default)
#     make build      # compile only (no board needed)
#     make upload     # compile + flash to the board
#     make monitor    # open the serial monitor
#     make ports      # list connected boards / serial ports
#     make help       # show all targets
#
#  Override the port if it is not /dev/ttyUSB0:
#     make upload PORT=/dev/ttyUSB1
# ============================================================================

# --- Config (verified for ESP32-WROOM-32) -----------------------------------
SKETCH_DIR := $(CURDIR)
FQBN       := esp32:esp32:esp32
PORT       ?= /dev/ttyUSB0
BAUD       ?= 115200
CLI        := arduino-cli

# Run upload then monitor in order even under "make -j".
.NOTPARALLEL:

# --- Default: flash the board and watch the boot log ------------------------
.PHONY: all
all: flash

# --- Compile only (no board required) ---------------------------------------
.PHONY: build
build:
	@echo ">> Compiling ($(FQBN))"
	$(CLI) compile --fqbn "$(FQBN)" "$(SKETCH_DIR)"

# --- Compile + upload -------------------------------------------------------
.PHONY: upload
upload: port-check
	@echo ">> Compiling + uploading to $(PORT)"
	$(CLI) compile --upload -p "$(PORT)" --fqbn "$(FQBN)" "$(SKETCH_DIR)"
	@echo ">> Done. Tap RESET if the sketch does not start automatically."

# --- Serial monitor ---------------------------------------------------------
.PHONY: monitor
monitor: port-check
	@echo ">> Monitor on $(PORT) @ $(BAUD)  (Ctrl-C to exit)"
	$(CLI) monitor -p "$(PORT)" -c baudrate=$(BAUD)

# --- Upload then open the monitor -------------------------------------------
.PHONY: flash
flash: upload monitor

# --- List connected boards / ports ------------------------------------------
.PHONY: ports
ports:
	$(CLI) board list

# --- Guard: fail early with help if the port is missing ---------------------
.PHONY: port-check
port-check:
	@if [ ! -e "$(PORT)" ]; then \
		echo "!! Port $(PORT) not found."; \
		echo "   Plug in the board's USB port, or: make upload PORT=/dev/ttyUSBx"; \
		echo "   If upload fails: hold BOOT, tap RESET, release BOOT, then retry."; \
		exit 1; \
	fi

# --- Help -------------------------------------------------------------------
.PHONY: help
help:
	@echo "Targets:"
	@echo "  make build     Compile only (no board needed)"
	@echo "  make upload    Compile + flash to $(PORT)"
	@echo "  make monitor   Serial monitor @ $(BAUD)"
	@echo "  make flash     Upload then monitor (default: 'make')"
	@echo "  make ports     List connected boards / serial ports"
	@echo ""
	@echo "Override the port:  make upload PORT=/dev/ttyUSB1"
