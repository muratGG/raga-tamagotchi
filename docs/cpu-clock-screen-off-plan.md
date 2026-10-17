# Plan: slow the CPU clock while the screen is off

Status: implemented (`clock_cpu_slow()` / `clock_cpu_fast()` in
`src/clock.c`); DIV_XTAL is set to 1 before bypassing rather than left as
found. Device test pending.

## Goal

With the screen off the main loop keeps spinning at the full CPU clock
Philips set up. Switch the CPU to the 24 MHz crystal while the screen is
off and back when it wakes, to save battery.

## What is known

- Philips' boot stage `____.2` sets the clocks from a table at `0xcc4`:
  CPU 261.8 MHz, HBUS 130.9 MHz (divider 2), EMI 130.9 MHz. Read from the
  firmware, not measured on the device.
- `play0` doesn't touch the clocks, so the game runs at those speeds.
- Game time (`DIGCTL_MICROSECONDS`), the LRADC (battery, buttons) and play0's
  delay loop run from the crystal: a CPU clock change doesn't affect them.
- The LCDIF runs from HBUS; it is idle while the screen is off.
- The only CPU-timed loop (`src/buttons.c:40`) runs once at start-up, at
  full speed.

## Steps

1. `src/clock.c` / `src/clock.h`: add `clock_cpu_slow()` and
   `clock_cpu_fast()`.
   - Slow: `HW_CLKCTRL_CLKSEQ_SET` (`0x800400e4`) = `BYPASS_CPU` (bit 7).
     clk_p then comes from the crystal through `CPU.DIV_XTAL`.
   - Fast: `HW_CLKCTRL_CLKSEQ_CLR` (`0x800400e8`) = `BYPASS_CPU`.
   - After either, wait for `HW_CLKCTRL_CPU` (`0x80040020`) `BUSY_REF_CPU`
     (bit 28) and `BUSY_REF_XTAL` (bit 29) to clear.
   - Leave the PLL on and `CPU.DIV_XTAL` (bits 16-25, 1 after reset) as it
     is; check its value once on the device if anything looks off.
2. `src/main.c` main loop: call `clock_cpu_slow()` right after
   `lcd_disable()`; in the wake-up branch call `clock_cpu_fast()` before
   `render()` / `fb_present()` / `lcd_enable()`.
3. `power_off()`: nothing to do; the next boot sets the clocks again.
4. `tools/screenshot.c`: empty stubs for the two functions.
5. `docs/HARDWARE.md`: Philips' clock settings and the bypass switch.
6. Build: `make` and `make build/host/screenshot` clean.

## Left out for now

- Lowering VDDD (Rockbox: 1.275 V at 261 MHz, lower at 64 MHz): more
  saving, but the order of voltage and frequency changes matters and a
  mistake hangs or resets the chip.
- `wfi` with `CLKCTRL_CPU.INTERRUPT_WAIT`: needs a wake-up interrupt (timer
  plus ICOLL set up), and the game uses no interrupts yet.

## Device test

- After the screen times out, a button press brings it back and drawing is
  smooth.
- Game time doesn't drift: clock and stats keep their pace with the screen
  off.
- Charge & play: charging carries on with the screen off.
- Battery: compare the mV drop over the same screen-off time (e.g. 1 h)
  with the previous build.

## Risk

No permanent risk: nothing is written to NAND and a reboot restores
Philips' clocks. If the clock isn't switched back, drawing is slow; if the
chip hangs, it stays hung (watchdog off) until the battery runs out or it
is reset.
