/* TRIAC phase control synchronised to the mains zero cross (spec §9) */
#ifndef TRIAC_H
#define TRIAC_H

#include <stdbool.h>
#include <stdint.h>

void triac_init(void);
void triac_poll(void);

/* Fade the load on or off; fade_mode 0..3 (DP 103 / DP 104) */
void triac_set(bool on, uint8_t fade_mode);

/* True once the level has reached the requested target */
bool triac_done(void);

/* Averaged mains period in 10 us ticks, 0 until acquired */
uint16_t triac_period(void);

#endif
