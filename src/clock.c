#include "clock.h"
#include "lcd.h"

#define HW_DIGCTL_MICROSECONDS 0x8001c0c0 /* also used by play0's delay loop */

#define HW_CLKCTRL_CPU          0x80040020
#define CPU_BUSY_REF            (3u << 28)    /* BUSY_REF_XTAL | BUSY_REF_CPU */
#define CPU_DIV_XTAL_BM         (0x7ffu << 16) /* DIV_XTAL + DIV_XTAL_FRAC_EN */
#define CPU_DIV_XTAL_1          (1u << 16)
#define HW_CLKCTRL_CLKSEQ_SET   0x800400e4
#define HW_CLKCTRL_CLKSEQ_CLR   0x800400e8
#define CLKSEQ_BYPASS_CPU       (1u << 7)

static unsigned last_us, ms, us_rem;

unsigned clock_us_raw(void) { return REG32(HW_DIGCTL_MICROSECONDS); }

void clock_init(void)
{
    last_us = clock_us_raw();
    ms = 0;
    us_rem = 0;
}

unsigned clock_ms(void)
{
    unsigned now = clock_us_raw();
    us_rem += now - last_us; /* unsigned subtraction handles the wrap */
    last_us = now;
    ms += us_rem / 1000;
    us_rem %= 1000;
    return ms;
}

static void cpu_wait_dividers(void)
{
    while (REG32(HW_CLKCTRL_CPU) & CPU_BUSY_REF)
        ;
}

/* Philips' boot (____.2) runs the CPU from the PLL at 261.8 MHz. Bypass
 * switches it to the 24 MHz crystal, through CPU.DIV_XTAL: set that to 1
 * first, which is harmless while the PLL path is in use. The PLL keeps
 * running, so switching back is immediate. */
void clock_cpu_slow(void)
{
    REG32(HW_CLKCTRL_CPU) = (REG32(HW_CLKCTRL_CPU) & ~CPU_DIV_XTAL_BM) | CPU_DIV_XTAL_1;
    cpu_wait_dividers();
    REG32(HW_CLKCTRL_CLKSEQ_SET) = CLKSEQ_BYPASS_CPU;
    cpu_wait_dividers();
}

void clock_cpu_fast(void)
{
    REG32(HW_CLKCTRL_CLKSEQ_CLR) = CLKSEQ_BYPASS_CPU;
    cpu_wait_dividers();
}
