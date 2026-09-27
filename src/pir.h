/* HS6605 digital PIR front end on PB12 (spec §11.1-11.2, §11.6) */
#ifndef PIR_H
#define PIR_H

#include <stdbool.h>
#include <stdint.h>

void pir_init(void);
void pir_poll(void);
void pir_set_sensitivity(uint8_t sensitivity);
bool pir_motion(void);

/* Warm-up finished */
bool pir_ready(void);

#endif
