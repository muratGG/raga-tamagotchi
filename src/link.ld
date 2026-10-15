ENTRY(_start)

/*
 * 0x60000 is free while we run, both from USB recovery and in a NAND boot:
 *   play0 (LCD init, logo)          0x01000 - 0x0e714
 *   NAND bootloader (mlc_/mmc_)     0x00000 - 0x3fbc8, stacks up to ~0x5e600
 *   Philips power init (____.0)     0x7c000 - 0x7c660
 * The bootloader processes the 'play' section that loads and calls us, so
 * it must stay intact; the original player (play1) loads after we return.
 */
SECTIONS
{
    . = 0x60000;
    .text : { *(.text.start) *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    .bss  : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
    __end = .;
    ASSERT(__end <= 0x7c000, "tama overlaps Philips' power init code at 0x7c000")
}
