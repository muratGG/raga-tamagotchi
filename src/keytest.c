/*
 * Button calibration screen, large decimal digits:
 *   top    (yellow): POWER_STS.PSWITCH level (0..3) - power slider and UP
 *   bottom (cyan)  : LRADC channel 1 (0..4095)      - resistor-ladder buttons
 *   top right (green, small): battery voltage in mV  - for the charge curve
 */
#include "lcd.h"
#include "power.h"

#define HW_POWER_STS        0x800440b0
#define POWER_STS_PSWITCH_BP 18
#define HW_LRADC_CTRL0_SET  0x80050004
#define HW_LRADC_CTRL0_CLR  0x80050008
#define HW_LRADC_CH(n)      (0x80050050 + (n) * 0x10)
#define LRADC_SFTRST_CLKGATE 0xc0000000
#define LRADC_CH_VALUE      0xfff
#define BUTTON_CHANNEL      1

#define SCALE 4 /* digit cell: 16x24 px */

static u32 read_pswitch(void) { return (REG32(HW_POWER_STS) >> POWER_STS_PSWITCH_BP) & 3; }

static u32 read_ladder(void)
{
    REG32(HW_LRADC_CTRL0_SET) = 1 << BUTTON_CHANNEL;
    for (volatile int n = 20000; n; n--)
        ;
    return REG32(HW_LRADC_CH(BUTTON_CHANNEL)) & LRADC_CH_VALUE;
}

int main(void)
{
    const u16 bg = RGB(0, 0, 0);
    const u16 yellow = RGB(255, 220, 60);
    const u16 cyan = RGB(80, 220, 255);
    const u16 green = RGB(80, 220, 90);
    u32 last_p = ~0u, last_l = ~0u, last_mv = ~0u;

    watchdog_disable();
    REG32(HW_LRADC_CTRL0_CLR) = LRADC_SFTRST_CLKGATE;
    battery_init();

    lcd_fill_rect(0, 0, LCD_W, LCD_H, bg);
    lcd_enable();

    for (;;) {
        u32 p = read_pswitch();
        u32 l = read_ladder();
        if (p != last_p) {
            lcd_dec(56, 4, p, 1, SCALE, yellow, bg);
            last_p = p;
        }
        if (l != last_l) {
            lcd_dec(32, 36, l, 4, SCALE, cyan, bg);
            last_l = l;
        }
        u32 mv = battery_mv();
        if (mv != last_mv) {
            lcd_dec(LCD_W - 32, 4, mv, 4, 2, green, bg);
            last_mv = mv;
        }
    }
}
