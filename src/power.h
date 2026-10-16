#ifndef RAGA_POWER_H
#define RAGA_POWER_H

/* Screen off, wait for the power slider to be let go, then cut power (the
 * slider turns it back on). */
void power_off(void);

/* Start the LRADC's automatic battery conversion: a delay channel triggers
 * channel 7 (battery) ten times a second and the result lands in
 * POWER_BATTMONITOR. Set up the way Rockbox does it on the STMP3700. */
void battery_init(void);

/* Battery voltage in millivolts, 0 until the first conversion. */
unsigned battery_mv(void);

/* 0..100 along a Li-ion discharge curve. */
int battery_percent(unsigned mv);

/* 5 V present on USB (POWER_STS.VDD5V_GT_VDDIO). While it is, the battery
 * voltage says nothing about the charge. */
int usb_power(void);

/* The battery charger is running (POWER_STS.CHRGSTS). */
int battery_charging(void);

/* Charge the battery while on USB power; call regularly (every loop).
 * Philips' pSay0 leaves the charger off, so the game runs it the way
 * Rockbox does on the STMP3700, with Philips' own currents: charge until
 * the current drops below the stop limit, top off for a while, then switch
 * off; give up after a timeout. now: clock_ms(). */
void charger_update(unsigned now);

#endif
