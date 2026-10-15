#ifndef RAGA_CLOCK_H
#define RAGA_CLOCK_H

/* Milliseconds since clock_init(), built on the 32-bit DIGCTL microsecond
 * counter (wraps every ~71 min; call clock_ms() at least that often). */
void clock_init(void);
unsigned clock_ms(void);
unsigned clock_us_raw(void);

#endif
