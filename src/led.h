/* Indicator LED PWM and effect engine (spec §10.1-10.3) */
#ifndef LED_H
#define LED_H

#include <stdint.h>

/* Output mask bits */
#define LED_WHITE   0x1u   /* PA7 + PB0 + PB1, TIM3 CH2/3/4 */
#define LED_RED     0x2u   /* PB15, TIM1 CH3N */
#define LED_GREEN   0x4u   /* PB13, TIM1 CH1N */
#define LED_BLUE    0x8u   /* PB14, TIM1 CH2N */

#define LED_CYAN    (LED_GREEN | LED_BLUE)
#define LED_YELLOW  (LED_RED | LED_GREEN)

enum {
  LED_OFF = 0,
  LED_STEADY = 1,
  LED_BLINK = 2,
  LED_BREATHE = 3,
  LED_LEVELS = 4,   /* each output at its own level (night-light) */
};

/* Index into the levels array of led_set_levels() */
enum { LED_CH_WHITE, LED_CH_RED, LED_CH_GREEN, LED_CH_BLUE, LED_CH_COUNT };

#define LED_FOREVER  0xFF

void led_init(void);
void led_poll(void);

/* Target brightness 0..100 */
void led_set_target(uint8_t level);

/* Select outputs and effect; count = number of blinks (LED_FOREVER = infinite) */
void led_set(uint8_t mask, uint8_t mode, uint8_t count);

/*
 * Steady, with each output at its own level 0..100 (LED_CH_*); 0 leaves it
 * off. Fades from what is lit now, one step per 20 ms like LED_STEADY.
 */
void led_set_levels(const uint8_t levels[LED_CH_COUNT]);

#endif
