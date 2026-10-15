#include "clock.h"
#include "lcd.h"

#define HW_DIGCTL_MICROSECONDS 0x8001c0c0 /* also used by play0's delay loop */

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
