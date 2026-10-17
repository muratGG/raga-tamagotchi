# Philips GoGear Raga (SA3RGA): hardware notes

What was found while porting, for anyone who wants to write their own code
for this player. Register names follow Rockbox's STMP3700 headers
(`firmware/target/arm/imx233/regs/stmp3700/`), which apply to the STMP3770.

## Identity

| | |
|---|---|
| SoC | SigmaTel/Freescale STMP3770, ARM926EJ-S (`Chip Major Rev ID` 0x37b0, ROM rev 2) |
| USB, normal mode | `0471:20e3` "Philips GoGear Raga", mass storage, 2048-byte sectors |
| USB, ROM recovery | `066f:3770` "Sigmatel ROM Recovery", HID, 48 bytes per transfer |
| Firmware | `.sb` v1.1, 16 sections, drive tag 0x50, all-zero key; 1.7.0 dated 2011-05-12 |
| Storage | 2 GB NAND: data drive, 23.5 MB system drive (tag 0x50), two 1 MB hidden drives |
| Battery | Li-ion PR332531, 3.7 V, 220 mAh (0.81 Wh) |

Recovery mode: switch off, hold volume + (UP), plug USB in.

## Boot flow

The ROM runs section `____`: four Philips init stages (power, clocks;
`____.3` also arms the RTC watchdog), then calls that pick the NAND driver
section (`mlc_` or `mmc_`). That driver is the boot loader: it reads the
image from the system drive and runs `host` (USB mode) or `play`/`pSay`
(player; `pSay` for charge & play).

`play` = `play0` (battery check, LCD init, boot logo; returns) then `play1`
(the player OS). The game sits between the two.

### RAM in use while `play` runs

| Range | User |
|---|---|
| `0x01000-0x0e714` | play0 |
| `0x00000-0x3fbc8` | boot loader code/data (`mlc_`; `mmc_` similar) |
| `0x445b8` / `0x4df78` up to ~`0x5e600` | boot loader stacks (`mmc_` / `mlc_`) |
| `0x60000-0x6df28` | the game (`src/link.ld`) |
| `0x7c000-0x7c660` | Philips power init (`____.0`) |

An earlier build at `0x30000` ran fine from USB but would have overwritten the
boot loader in a NAND boot: keep clear of the ranges above.

## Display

128x64 RGB565 TFT behind the LCDIF block (`0x80030000`), 8-bit bus. The
controller speaks MIPI DCS: `0x2a` columns (one byte each: 0..127), `0x2b`
rows, visible rows `0x20..0x5f`, `0x2c` memory write, two bytes per pixel,
high byte first. `0x28`/`0x29` display off/on.

play0 leaves two ARM-mode routines resident (call with even addresses):

| Address | |
|---|---|
| `0x1f3c` | `write(is_data, byte)`: one command or data byte |
| `0x1df0` | `enable(on)`: `0x29` + backlight on, or backlight off + `0x28` |

Bulk pixel data goes straight to the LCDIF: wait for `CTRL.RUN` to clear,
clear `CTRL.DATA_SWIZZLE`, set `RUN | DATA_SELECT | count` (8-bit word length:
count in bytes), push 32-bit words to `DATA` (first byte in bits 7:0) while
`STAT.TXFIFO_FULL` is clear. See `src/lcd.c`.

Backlight: GPIO bank 0 pin 14 (`DOUT0` bit 14).

## Buttons

| Button | Source | Reading |
|---|---|---|
| power slider | `POWER_STS.PSWITCH` (bits 18-19) | 1 |
| UP | `POWER_STS.PSWITCH` | 3 |
| DOWN | LRADC channel 1 | ~7 |
| PLAY | LRADC channel 1 | ~677 |
| RIGHT | LRADC channel 1 | ~1275 |
| MENU | LRADC channel 1 | ~1875 |
| BACK | LRADC channel 1 | ~2521 |
| LEFT | LRADC channel 1 | ~3127 |
| none | | PSWITCH 0, LRADC ~4095 |

LRADC: clear `CTRL0.SFTRST|CLKGATE`, schedule channel 1 via `CTRL0` bits 0-7,
read `CH1` bits 0-11. Thresholds halfway between readings work well.
`make run-keytest` shows both values live.

## Other

- Time: `DIGCTL_MICROSECONDS` (`0x8001c0c0`), free-running 32-bit microseconds.
- Clocks: `____.2` sets them from a table at `0xcc4`: CPU 261.8 MHz from the
  PLL, HBUS 130.9 MHz (divider 2), EMI 130.9 MHz; play0 leaves them. While
  the screen is off the game runs the CPU from the 24 MHz crystal:
  `CLKCTRL_CPU.DIV_XTAL` (`0x80040020` bits 16-25) = 1, then
  `CLKCTRL_CLKSEQ.BYPASS_CPU` (`0x800400e0` bit 7) set; clearing it goes
  back to the PLL. Time and the LRADC run from the crystal either way.
- RTC watchdog: armed by `____.3`; clear `RTC_CTRL.WATCHDOGEN`
  (`0x8005c008` = 0x10) or the chip resets into the NAND firmware.
- Power off: `POWER_RESET` (`0x800440e0`) = `0x3e770001`. First wait for the
  power slider to be released and clear `RTC_PERSISTENT0.AUTO_RESTART`
  (and `ALARM_WAKE_EN`; `0x8005c068` = `0x20002`, then wait for
  `RTC_STAT.NEW_REGS` to clear), or the player switches straight back on.
  Which of the two was the cause was not isolated.
- Battery: as in Rockbox, LRADC delay channel 0 triggers channel 7 (source 7,
  battery) and itself every 200 ticks of 2 kHz; `LRADC_CONVERSION` set to
  automatic, Li-ion scale, copies the result to `POWER_BATTMONITOR.BATT_VAL`
  (`0x800440d0` bits 16-25, 8 mV per step). See `src/power.c`;
  `make run-keytest` shows the mV. The percentage curve is Rockbox's Sansa
  Fuze+ one, not yet measured on the Raga.
- USB power: `POWER_STS.VDD5V_GT_VDDIO` (bit 4); `VBUSVALID` needs
  `POWER_5VCTRL` set up first. Charger running: `POWER_STS.CHRGSTS` (bit 10).
- Charging: Philips' `pSay0` (charge & play) leaves the charger off, so the
  game runs it (`charger_update()` in `src/power.c`): `POWER_CHARGE`
  (`0x80044030`) `BATTCHRG_I` bits 0-5 = 10/20/50/100/200/400 mA,
  `STOP_ILIMIT` bits 8-11 = 10/20/50/100 mA, clear `PWD_BATTCHRG` (bit 16) to
  start. Rockbox also switches `POWER_5VCTRL` to the DC-DC on 5 V; we leave
  power as pSay0 set it.
- Philips' charger settings: `pSay1` builds a Freescale `ddi_bc` config on
  the stack (from `0xed1e8`) and passes it to the driver's init (`0xee238`):
  100 ms state machine, 100 mA/s ramp, 22 mA below 3000 mV (1 h timeout),
  4200 mV, 120 mA, stop at 16 mA, 4 h timeout, 30 min top-off, die
  temperature 71 C high / 56 C low with 30 mA safe current. The game uses
  the currents, the 3000 mV threshold and the timeouts.
- Also present but unused so far: FM tuner, microphone, audio out.
