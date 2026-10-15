/*
 * Raga buttons, measured with keytest:
 *   PSWITCH level (POWER_STS bits 18-19): 0 idle, 1 power slider, 3 UP
 *   LRADC channel 1 resistor ladder (0..4095, idle 4095):
 *     DOWN 7, PLAY 677, RIGHT 1275, MENU 1875, BACK 2521, LEFT 3127
 * Ladder thresholds sit halfway between neighbouring readings.
 */
#include "buttons.h"
#include "lcd.h"

#define HW_POWER_STS         0x800440b0
#define POWER_STS_PSWITCH_BP 18
#define HW_LRADC_CTRL0_SET   0x80050004
#define HW_LRADC_CTRL0_CLR   0x80050008
#define HW_LRADC_CH(n)       (0x80050050 + (n) * 0x10)
#define LRADC_SFTRST_CLKGATE 0xc0000000
#define LRADC_CH_VALUE       0xfff
#define LADDER_CHANNEL       1

static const struct { unsigned max; unsigned button; } ladder[] = {
    {341, BTN_DOWN},
    {975, BTN_PLAY},
    {1574, BTN_RIGHT},
    {2197, BTN_MENU},
    {2823, BTN_BACK},
    {3610, BTN_LEFT},
};

static unsigned stable;

void buttons_init(void)
{
    REG32(HW_LRADC_CTRL0_CLR) = LRADC_SFTRST_CLKGATE;
    stable = 0;
}

static unsigned read_ladder(void)
{
    REG32(HW_LRADC_CTRL0_SET) = 1 << LADDER_CHANNEL;
    for (volatile int n = 20000; n; n--)
        ;
    return REG32(HW_LRADC_CH(LADDER_CHANNEL)) & LRADC_CH_VALUE;
}

static unsigned read_raw(void)
{
    unsigned state = 0;

    switch ((REG32(HW_POWER_STS) >> POWER_STS_PSWITCH_BP) & 3) {
    case 1: state |= BTN_POWER; break;
    case 3: state |= BTN_UP; break;
    }

    unsigned v = read_ladder();
    for (unsigned i = 0; i < sizeof(ladder) / sizeof(ladder[0]); i++)
        if (v <= ladder[i].max) {
            state |= ladder[i].button;
            break;
        }
    return state;
}

unsigned buttons_read(void)
{
    unsigned a = read_raw();

    /* Accept a change only when two consecutive samples agree. */
    if (a != stable && a == read_raw())
        stable = a;
    return stable;
}
