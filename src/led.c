#include "led.h"

#include "board.h"
#include "timebase.h"

#define PWM_ARR      999     /* 48 kHz at 48 MHz */
#define STEP_MS      20
#define BLINK_MS     333
#define HOLD_STEPS   50      /* 1 s at 20 ms per step */

/* Level 0..100 -> CCR (spec §10.2) */
static const uint16_t gamma[101] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 19, 22, 25, 28, 32, 36, 40, 44, 48, 52,
  57, 62, 67, 72, 78, 84, 89, 96, 102, 108, 115, 122, 129, 136, 144, 151, 159, 167, 176,
  184, 193, 202, 211, 220, 229, 239, 249, 259, 269, 280, 290, 301, 312, 323, 335, 347,
  358, 370, 383, 395, 408, 421, 434, 447, 460, 474, 488, 502, 516, 530, 545, 560, 575,
  590, 605, 621, 637, 653, 669, 685, 702, 719, 736, 753, 770, 788, 806, 824, 842, 860,
  879, 898, 917, 936, 955, 975, 1000,
};

enum { BR_UP, BR_HOLD_HIGH, BR_DOWN, BR_HOLD_LOW };

static struct {
  uint8_t mask;
  uint8_t mode;
  uint8_t count;
  uint8_t target;
  uint8_t level;
  uint8_t phase;      /* blink: 1 = lit; breathe: BR_* */
  uint8_t hold;
  sw_timer_t step;
} led;

static void apply(uint8_t mask, uint8_t level)
{
  uint16_t ccr = gamma[level > 100 ? 100 : level];
  uint16_t white = (mask & LED_WHITE) ? ccr : 0;

  TIM3->CCR2 = white;
  TIM3->CCR3 = white;
  TIM3->CCR4 = white;
  TIM1->CCR3 = (mask & LED_RED) ? ccr : 0;
  /* Amber status colour: green at 1/5 of the level */
  if (mask == LED_AMBER)
    TIM1->CCR1 = gamma[level / 5];
  else
    TIM1->CCR1 = (mask & LED_GREEN) ? ccr : 0;
  TIM1->CCR2 = (mask & LED_BLUE) ? ccr : 0;
}

static void pwm_init(TIM_TypeDef *tim)
{
  tim->PSC = 0;
  tim->ARR = PWM_ARR;
  /* PWM mode 1 on all four channels, preload off */
  tim->CCMR1 = (6u << 4) | (6u << 12);
  tim->CCMR2 = (6u << 4) | (6u << 12);
}

void led_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
  RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

  /* TIM3 CH2/3/4, inverted polarity (CCER = 0x3330) */
  pwm_init(TIM3);
  TIM3->CCER = 0x3330;
  TIM3->CR1 = TIM_CR1_CEN;

  /* TIM1 CH1N/2N/3N, active high (CCER = 0x0444) */
  pwm_init(TIM1);
  TIM1->CCER = 0x0444;
  TIM1->BDTR = TIM_BDTR_MOE;
  TIM1->CR1 = TIM_CR1_CEN;

  apply(0, 0);

  gpio_af(GPIOA, 7, 1);
  gpio_af(GPIOB, 0, 1);
  gpio_af(GPIOB, 1, 1);
  gpio_af(GPIOB, 13, 2);
  gpio_af(GPIOB, 14, 2);
  gpio_af(GPIOB, 15, 2);
}

void led_set_target(uint8_t level)
{
  led.target = level > 100 ? 100 : level;
}

void led_set(uint8_t mask, uint8_t mode, uint8_t count)
{
  if (mask == led.mask && mode == led.mode && count == led.count)
    return;

  led.mask = mask;
  led.mode = mode;
  led.count = count;
  led.hold = 0;
  timer_set_ms(&led.step, 0);

  switch (mode)
  {
  case LED_BLINK:
    led.phase = 0;
    break;
  case LED_BREATHE:
    led.level = 0;
    led.phase = BR_UP;
    break;
  default:
    break;
  }
}

static void step_toward_target(void)
{
  if (led.level < led.target)
    led.level++;
  else if (led.level > led.target)
    led.level--;
}

void led_poll(void)
{
  switch (led.mode)
  {
  case LED_STEADY:
    if (timer_expired(&led.step))
    {
      timer_set_ms(&led.step, STEP_MS);
      step_toward_target();
    }
    apply(led.mask, led.level);
    break;

  case LED_BLINK:
    led.level = led.target;
    if (timer_expired(&led.step))
    {
      timer_set_ms(&led.step, BLINK_MS);
      if (led.phase)
      {
        led.phase = 0;
        if (led.count != LED_FOREVER && led.count > 0 && --led.count == 0)
        {
          led.mode = LED_OFF;
          break;
        }
      }
      else
      {
        led.phase = 1;
      }
    }
    apply(led.phase ? led.mask : 0, led.level);
    break;

  case LED_BREATHE:
    if (timer_expired(&led.step))
    {
      timer_set_ms(&led.step, STEP_MS);
      switch (led.phase)
      {
      case BR_UP:
        if (led.level < led.target)
          led.level++;
        else
          led.phase = BR_HOLD_HIGH, led.hold = 0;
        break;
      case BR_HOLD_HIGH:
        if (++led.hold >= HOLD_STEPS)
          led.phase = BR_DOWN;
        break;
      case BR_DOWN:
        if (led.level > 0)
          led.level--;
        else
          led.phase = BR_HOLD_LOW, led.hold = 0;
        break;
      default:
        if (++led.hold >= HOLD_STEPS)
          led.phase = BR_UP;
        break;
      }
    }
    apply(led.mask, led.level);
    break;

  default:
    apply(0, 0);
    break;
  }
}
