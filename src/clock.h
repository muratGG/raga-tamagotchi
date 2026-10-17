#ifndef RAGA_CLOCK_H
#define RAGA_CLOCK_H

/* Milliseconds since clock_init(), built on the 32-bit DIGCTL microsecond
 * counter (wraps every ~71 min; call clock_ms() at least that often). */
void clock_init(void);
unsigned clock_ms(void);
unsigned clock_us_raw(void);

/* CPU from the 24 MHz crystal instead of the PLL (261.8 MHz) and back, for
 * while the screen is off. Time keeping runs from the crystal either way. */
void clock_cpu_slow(void);
void clock_cpu_fast(void);

#endif
