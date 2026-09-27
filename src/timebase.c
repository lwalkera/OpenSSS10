#include "timebase.h"

#include "ft32f0xx.h"

static sw_timer_t now;
static uint16_t last_cnt;

void timebase_init(void)
{
  /* TIM6: 1 kHz free-running counter, no interrupt */
  RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
  TIM6->PSC = 47999;
  TIM6->ARR = 0xFFFF;
  TIM6->EGR = TIM_EGR_UG;
  TIM6->CR1 = TIM_CR1_CEN;
  last_cnt = (uint16_t)TIM6->CNT;

  /* SysTick: free-running 24-bit core-clock counter for microsecond delays */
  SysTick->LOAD = 0x00FFFFFF;
  SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

void clock_update(void)
{
  uint16_t cnt = (uint16_t)TIM6->CNT;
  uint32_t ms = now.ms + (uint16_t)(cnt - last_cnt);

  last_cnt = cnt;
  now.sec += ms / 1000;
  now.ms = (uint16_t)(ms % 1000);
}

sw_timer_t clock_now(void)
{
  return now;
}

uint32_t clock_seconds(void)
{
  return now.sec;
}

void timer_set_ms(sw_timer_t *t, uint32_t ms)
{
  uint32_t total = now.ms + ms;

  t->sec = now.sec + total / 1000;
  t->ms = (uint16_t)(total % 1000);
}

void timer_set_s(sw_timer_t *t, uint32_t s)
{
  t->sec = now.sec + s;
  t->ms = now.ms;
}

bool timer_expired(const sw_timer_t *t)
{
  return now.sec > t->sec || (now.sec == t->sec && now.ms >= t->ms);
}

void delay_ms(uint32_t ms)
{
  uint16_t start = (uint16_t)TIM6->CNT;

  while ((uint16_t)((uint16_t)TIM6->CNT - start) < ms)
  {
  }
}

void delay_cycles(uint32_t cycles)
{
  uint32_t start = SysTick->VAL;

  /* SysTick counts down */
  while (((start - SysTick->VAL) & 0x00FFFFFF) < cycles)
  {
  }
}
