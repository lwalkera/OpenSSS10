/* Ambient light level and dark flag (spec §11.5) */
#ifndef LIGHT_H
#define LIGHT_H

#include <stdbool.h>
#include <stdint.h>

void light_init(void);
void light_cb(const uint16_t *buf, uint16_t n);

/* Dark threshold 0..100 from the schedule's light preset */
void light_set_dark_threshold(uint8_t threshold);

uint8_t light_level(void);    /* 0..100, higher = brighter */
bool light_dark(void);

/* Span of levels seen since light_reset_span (factory test) */
void light_reset_span(void);
uint8_t light_span(void);

#endif
