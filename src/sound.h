/* Sound level and trigger from microphone bursts (spec §11.4) */
#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>
#include <stdint.h>

void sound_init(void);
void sound_cb(const uint16_t *buf, uint16_t n);

/* Threshold on the 0..255 level scale (DP 106 20..100 scaled by 255 / 100):
   the quietest level is 51, and 255 disables the trigger */
#define SOUND_THR_MIN      51
#define SOUND_THR_OFF      255
#define SOUND_THR_DEFAULT  166   /* stock 65 */

void sound_set_threshold(uint8_t threshold);

uint8_t sound_level(void);            /* last level 0..255 */
bool sound_triggered(void);           /* trigger from the last burst */
uint8_t sound_take_peak(void);        /* peak level since the last call */

#endif
