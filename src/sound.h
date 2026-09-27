/* Sound level and trigger from microphone bursts (spec §11.4) */
#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>
#include <stdint.h>

void sound_init(void);
void sound_cb(const uint16_t *buf, uint16_t n);

/* Threshold 20..100 (DP 106); 100 disables the trigger */
void sound_set_threshold(uint8_t threshold);

uint8_t sound_level(void);            /* last level 0..100 */
bool sound_triggered(void);           /* trigger from the last burst */
uint8_t sound_take_peak(void);        /* peak level since the last call */

#endif
