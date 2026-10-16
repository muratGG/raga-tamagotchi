#include "power.h"
#include "lcd.h"

#define HW_POWER_RESET      0x800440e0
#define POWER_RESET_KEY     (0x3e77u << 16)
#define POWER_RESET_PWD     1u
#define HW_POWER_STS        0x800440b0
#define POWER_STS_PSWITCH_BP 18
#define POWER_STS_CHRGSTS    (1u << 10)
#define POWER_STS_VDD5V_GT_VDDIO (1u << 4)

#define HW_POWER_CHARGE_SET  0x80044034
#define HW_POWER_CHARGE_CLR  0x80044038
#define CHARGE_PWD_BATTCHRG  (1u << 16)
#define CHARGE_STOP_ILIMIT   (0xfu << 8)   /* bits: 10, 20, 50, 100 mA */
#define CHARGE_BATTCHRG_I    0x3fu         /* bits: 10, 20, 50, 100, 200, 400 mA */
#define CHARGE_STOP_10MA     (1u << 8)
#define CHARGE_I_20MA        (1u << 1)
#define CHARGE_I_120MA       ((1u << 3) | (1u << 1))

/* Philips' own charger settings, from the configuration pSay1 passes to
 * Freescale's battery charger driver (at 0xed1e8 in pSay1): 120 mA, stop
 * at 16 mA (the register has 10 mA steps; Philips' setter rounds down),
 * 22 mA below 3000 mV, 30 min top-off, 4 h timeout. Its die temperature
 * limit (71 C) and 100 mA/s ramp are left out. The cell (PR332531,
 * 220 mAh) gets the usual 0.5C, 0.1C while conditioning. */
#define CHARGE_FULL          (CHARGE_I_120MA | CHARGE_STOP_10MA)
#define CHARGE_CONDITIONING  (CHARGE_I_20MA | CHARGE_STOP_10MA)
#define CONDITIONING_MV      3000u
#define CHARGE_SETTLE_MS     2000u                 /* before CHRGSTS means anything */
#define CHARGE_TIMEOUT_MS    (4u * 3600u * 1000u)
#define TOPOFF_MS            (30u * 60u * 1000u)

#define HW_RTC_STAT              0x8005c010
#define RTC_STAT_NEW_REGS        0xff00     /* shadow writes still in flight */
#define HW_RTC_PERSISTENT0_CLR   0x8005c068
#define RTC_AUTO_RESTART         (1u << 17)
#define RTC_ALARM_WAKE_EN        (1u << 1)

#define HW_POWER_BATTMONITOR 0x800440d0
#define BATT_VAL_BP          16          /* 10 bits, 8 mV per step */
#define BATT_VAL_BM          0x3ff

#define HW_LRADC_CTRL2_CLR      0x80050028
#define LRADC_DIVIDE_BY_TWO_BP  24
#define HW_LRADC_CH_CLR(n)      (0x80050058 + (n) * 0x10)
#define LRADC_CH_SAMPLES_ACC    0x3f000000  /* NUM_SAMPLES + ACCUMULATE */
#define HW_LRADC_DELAY(n)       (0x800500d0 + (n) * 0x10)
#define HW_LRADC_DELAY_SET(n)   (HW_LRADC_DELAY(n) + 4)
#define LRADC_DELAY_KICK        (1u << 20)
#define HW_LRADC_CONVERSION_SET 0x80050134
#define HW_LRADC_CONVERSION_CLR 0x80050138
#define LRADC_CONV_AUTOMATIC    (1u << 20)
#define LRADC_CONV_SCALE_BM     (3u << 16)
#define LRADC_CONV_SCALE_LI_ION (2u << 16)
#define HW_LRADC_CTRL4_SET      0x80050144
#define HW_LRADC_CTRL4_CLR      0x80050148
#define LRADC7SELECT_BP         28

#define BATT_CHANNEL 7  /* channel 7, source 7 = battery */
#define BATT_DELAY   0  /* delay channel; the counter runs at 2 kHz */
#define BATT_TICKS   200

void power_off(void)
{
    lcd_disable();
    watchdog_disable();
    /* A slider still held when power goes would switch us straight back on. */
    while ((REG32(HW_POWER_STS) >> POWER_STS_PSWITCH_BP) & 3)
        ;
    /* The RTC's persistent bits survive power-down: with AUTO_RESTART set
     * the chip comes straight back up, so clear it (and alarm wake-up).
     * Writes go through a shadow register; wait until it has taken them. */
    while (REG32(HW_RTC_STAT) & RTC_STAT_NEW_REGS)
        ;
    REG32(HW_RTC_PERSISTENT0_CLR) = RTC_AUTO_RESTART | RTC_ALARM_WAKE_EN;
    while (REG32(HW_RTC_STAT) & RTC_STAT_NEW_REGS)
        ;
    REG32(HW_POWER_RESET) = POWER_RESET_KEY | POWER_RESET_PWD;
    for (;;)
        ;
}

void battery_init(void)
{
    REG32(HW_LRADC_CTRL2_CLR) = (1u << BATT_CHANNEL) << LRADC_DIVIDE_BY_TWO_BP;
    REG32(HW_LRADC_CTRL4_CLR) = 0xfu << LRADC7SELECT_BP;
    REG32(HW_LRADC_CTRL4_SET) = (u32)BATT_CHANNEL << LRADC7SELECT_BP;
    REG32(HW_LRADC_CH_CLR(BATT_CHANNEL)) = LRADC_CH_SAMPLES_ACC;

    /* trigger the battery channel and this delay again: endless loop */
    REG32(HW_LRADC_DELAY(BATT_DELAY)) = ((1u << BATT_CHANNEL) << 24)
                                      | ((1u << BATT_DELAY) << 16) | BATT_TICKS;
    REG32(HW_LRADC_DELAY_SET(BATT_DELAY)) = LRADC_DELAY_KICK;

    REG32(HW_LRADC_CONVERSION_CLR) = LRADC_CONV_SCALE_BM;
    REG32(HW_LRADC_CONVERSION_SET) = LRADC_CONV_SCALE_LI_ION | LRADC_CONV_AUTOMATIC;
}

/* Not POWER_STS.VBUSVALID: it only works once POWER_5VCTRL is set up for
 * it, which Rockbox does and we don't. */
int usb_power(void)
{
    return (REG32(HW_POWER_STS) & POWER_STS_VDD5V_GT_VDDIO) != 0;
}

int battery_charging(void)
{
    return (REG32(HW_POWER_STS) & POWER_STS_CHRGSTS) != 0;
}

static enum { CH_OFF, CH_CHARGING, CH_TOPOFF, CH_DONE } charge_state;
static unsigned charge_t0, charge_settings;

static void charger_off(void)
{
    REG32(HW_POWER_CHARGE_SET) = CHARGE_PWD_BATTCHRG;
}

/* A deeply discharged cell gets a trickle until it is above 3000 mV. */
static void charger_set_current(void)
{
    unsigned want = battery_mv() < CONDITIONING_MV ? CHARGE_CONDITIONING : CHARGE_FULL;

    if (want == charge_settings)
        return;
    REG32(HW_POWER_CHARGE_CLR) = CHARGE_STOP_ILIMIT | CHARGE_BATTCHRG_I;
    REG32(HW_POWER_CHARGE_SET) = want;
    charge_settings = want;
}

void charger_update(unsigned now)
{
    if (!usb_power()) {
        if (charge_state != CH_OFF)
            charger_off();
        charge_state = CH_OFF;
        return;
    }
    switch (charge_state) {
    case CH_OFF:
        charge_settings = 0;
        charger_set_current();
        REG32(HW_POWER_CHARGE_CLR) = CHARGE_PWD_BATTCHRG;
        charge_state = CH_CHARGING;
        charge_t0 = now;
        break;
    case CH_CHARGING:
        charger_set_current();
        if (now - charge_t0 >= CHARGE_TIMEOUT_MS) { /* charger or cell broken */
            charger_off();
            charge_state = CH_DONE;
        } else if (now - charge_t0 >= CHARGE_SETTLE_MS && !battery_charging()) {
            charge_state = CH_TOPOFF;               /* current below the stop limit */
            charge_t0 = now;
        }
        break;
    case CH_TOPOFF:
        if (now - charge_t0 >= TOPOFF_MS) {
            charger_off();
            charge_state = CH_DONE;
        }
        break;
    case CH_DONE: /* until USB comes out */
        break;
    }
}

unsigned battery_mv(void)
{
    return ((REG32(HW_POWER_BATTMONITOR) >> BATT_VAL_BP) & BATT_VAL_BM) * 8;
}

/* mV at 0%, 10%, ... 100% while discharging. Rockbox's Sansa Fuze+ curve
 * (also an STMP37xx-class player with a small Li-ion cell); not yet
 * measured on the Raga. */
static const unsigned short discharge_mv[11] = {
    3100, 3650, 3720, 3750, 3780, 3820, 3880, 4000, 4040, 4125, 4230,
};

int battery_percent(unsigned mv)
{
    if (mv <= discharge_mv[0])
        return 0;
    for (int i = 1; i < 11; i++)
        if (mv < discharge_mv[i])
            return (i - 1) * 10 + (int)(mv - discharge_mv[i - 1]) * 10
                                  / (discharge_mv[i] - discharge_mv[i - 1]);
    return 100;
}
