#include "power.h"
#include "lcd.h"

#define HW_POWER_RESET      0x800440e0
#define POWER_RESET_KEY     (0x3e77u << 16)
#define POWER_RESET_PWD     1u

void power_off(void)
{
    lcd_disable();
    watchdog_disable();
    REG32(HW_POWER_RESET) = POWER_RESET_KEY | POWER_RESET_PWD;
    for (;;)
        ;
}
