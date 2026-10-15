# Raga Tamagotchi
#
#   make tools        build the Rockbox imx tools into tools/bin (once)
#   make              build/raga-tama-fw.sb (to install) and the RAM images
#   make run          run the Tamagotchi from RAM (device in recovery mode)
#   make run-keytest  run the button calibration screen from RAM
#   make clean

TOOLS   := tools/bin
CC      := arm-none-eabi-gcc
CFLAGS  := -mcpu=arm926ej-s -marm -Os -ffreestanding -fno-builtin -nostdlib -Wall -Wextra
LDFLAGS := -T src/link.ld -lgcc
SHA1    := $(shell command -v sha1sum || echo shasum)

# Your own dump of the stock firmware (README: Install, step 2). Only
# SA3RGA firmware 1.7.0 is supported; this is the checksum of that image.
STOCK_FW   := firmware/raga-stock.sb
STOCK_SHA1 := 89dde5822b22ce61561ba7610b3936b1ac74e26c

HEADERS  := $(wildcard src/*.h)
# link order kept as built for prebuilt/raga-tama-fw.sb, so it reproduces
TAMA_SRC := src/start.S src/main.c src/lcd.c src/buttons.c src/fb.c src/clock.c \
            src/power.c src/sprites.c src/gameutil.c src/flappy.c src/snake.c \
            src/catch.c src/runner.c
KEYTEST_SRC := src/start.S src/keytest.c src/lcd.c

all: build/raga-tama-fw.sb build/tama-ram.sb build/keytest-ram.sb

tools:
	./tools/build-tools.sh

$(TOOLS)/%:
	@echo "missing $@: run 'make tools' first" && false

# Philips' parts (init stages, play0, NAND bootloader, ...) come from the
# stock image, unpacked here; the stamp also pins the exact firmware.
$(STOCK_FW):
	@echo "missing $@: dump the firmware from your Raga first (README: Install)" && false

build/fw/.stamp: $(STOCK_FW) | $(TOOLS)/sbtoelf
	@echo "$(STOCK_SHA1)  $<" | $(SHA1) -c - >/dev/null || \
		(echo "$< is not the SA3RGA stock firmware 1.7.0 this project supports" && false)
	rm -rf build/fw && mkdir -p build/fw
	$(TOOLS)/sbtoelf -n -z -o build/fw/raga $< >/dev/null
	touch $@

build/tama.elf: $(TAMA_SRC) $(HEADERS) src/link.ld
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(TAMA_SRC) $(LDFLAGS)

build/keytest.elf: $(KEYTEST_SRC) $(HEADERS) src/link.ld
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(KEYTEST_SRC) $(LDFLAGS)

build/raga-tama-fw.sb: firmware/install.db build/tama.elf build/fw/.stamp | $(TOOLS)/elftosb
	$(TOOLS)/elftosb -z -c $< -o $@

build/tama-ram.sb: firmware/ram-tama.db build/tama.elf build/fw/.stamp | $(TOOLS)/elftosb
	$(TOOLS)/elftosb -z -c $< -o $@

build/keytest-ram.sb: firmware/ram-keytest.db build/keytest.elf build/fw/.stamp | $(TOOLS)/elftosb
	$(TOOLS)/elftosb -z -c $< -o $@

run: build/tama-ram.sb | $(TOOLS)/sbloader
	sudo $(TOOLS)/sbloader $<

run-keytest: build/keytest-ram.sb | $(TOOLS)/sbloader
	sudo $(TOOLS)/sbloader $<

clean:
	rm -rf build

.PHONY: all tools run run-keytest clean
