/* Pin assignments for the FT32F030C8AT7 smart switch (spec §4) */
#ifndef BOARD_H
#define BOARD_H

#include "ft32f0xx.h"

/* Analog inputs */
#define MIC_ADC_CH        1               /* PA1 */
#define LIGHT_ADC_CH      6               /* PA6 */

/* White LED PWM: PA7, PB0, PB1 = TIM3 CH2/3/4 (AF1, inverted) */
/* RGB PWM: PB13/PB14/PB15 = TIM1 CH1N/2N/3N (AF2) */

/* TRIAC gate (opto LED), active low */
#define GATE_PORT         GPIOB
#define GATE_PIN          3

/* Wall-switch mains sense (three-way variant, unpopulated) */
#define WALLSW_PORT       GPIOB
#define WALLSW_PIN        4

/* Mains zero-cross optocoupler, one rising edge per cycle, EXTI5 */
#define ZC_PORT           GPIOB
#define ZC_PIN            5

/* Tuya module reset, active low */
#define MODRST_PORT       GPIOB
#define MODRST_PIN        10

/* HS6605 single-wire data */
#define PIR_PORT          GPIOB
#define PIR_PIN           12

/* Push button, active low */
#define BUTTON_PORT       GPIOF
#define BUTTON_PIN        7

/* GPIO MODER values */
#define GPIO_MODE_IN      0u
#define GPIO_MODE_OUT     1u
#define GPIO_MODE_AF      2u
#define GPIO_MODE_ANALOG  3u

/* GPIO PUPDR values */
#define GPIO_PULL_NONE    0u
#define GPIO_PULL_UP      1u

/* GPIO OSPEEDR value for high speed */
#define GPIO_SPEED_HIGH   3u

static inline void gpio_mode(GPIO_TypeDef *port, unsigned pin, unsigned mode)
{
  port->MODER = (port->MODER & ~(3u << (pin * 2))) | (mode << (pin * 2));
}

static inline void gpio_pull(GPIO_TypeDef *port, unsigned pin, unsigned pull)
{
  port->PUPDR = (port->PUPDR & ~(3u << (pin * 2))) | (pull << (pin * 2));
}

static inline void gpio_speed_high(GPIO_TypeDef *port, unsigned pin)
{
  port->OSPEEDR |= GPIO_SPEED_HIGH << (pin * 2);
}

static inline void gpio_af(GPIO_TypeDef *port, unsigned pin, unsigned af)
{
  unsigned reg = pin >> 3, shift = (pin & 7) * 4;
  port->AFR[reg] = (port->AFR[reg] & ~(0xFu << shift)) | (af << shift);
  gpio_mode(port, pin, GPIO_MODE_AF);
  gpio_speed_high(port, pin);
}

static inline void gpio_set(GPIO_TypeDef *port, unsigned pin)
{
  port->BSRR = 1u << pin;
}

static inline void gpio_clear(GPIO_TypeDef *port, unsigned pin)
{
  port->BRR = 1u << pin;
}

static inline unsigned gpio_read(GPIO_TypeDef *port, unsigned pin)
{
  return (port->IDR >> pin) & 1u;
}

#endif
