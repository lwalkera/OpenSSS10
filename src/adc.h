/* TIM15-triggered ADC sampler alternating microphone and light (spec §11.3) */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

typedef void (*adc_cb_t)(const uint16_t *buf, uint16_t n);

void adc_init(void);
void adc_set_callbacks(adc_cb_t sound_cb, adc_cb_t light_cb);
void adc_sampler_poll(void);

#endif
