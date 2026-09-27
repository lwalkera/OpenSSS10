#include "triac.h"

#include "board.h"
#include "triac_calc.h"

#define PERIOD_MIN      1428   /* 70 Hz */
#define PERIOD_MAX      2501   /* 40 Hz */
#define ACQUIRE_COUNT   16
#define RESYNC_COUNT    10
#define RUN_AVG_COUNT   30

typedef void (*zc_state_fn)(uint16_t period);

static void zc_acquire(uint16_t period);
static void zc_resync(uint16_t period);
static void zc_run(uint16_t period);

/* Shared with the ISRs */
static volatile zc_state_fn zc_state = zc_acquire;
static volatile uint16_t period_avg;        /* published average, 0 = invalid */
static gate_table_t tables[2];
static volatile uint8_t active;             /* table used by the ISR */
static volatile bool table_pending;         /* back table ready to swap in */
static volatile uint8_t ev_idx;

/* ISR-private accumulators */
static uint32_t acc_sum;
static uint8_t acc_n;

/* Main-loop state */
static uint16_t level;
static uint16_t target;
static uint8_t fade;
static bool level_dirty = true;

static inline void gate_on(void)
{
  gpio_clear(GATE_PORT, GATE_PIN);
}

static inline void gate_off(void)
{
  gpio_set(GATE_PORT, GATE_PIN);
}

static bool period_valid(uint16_t p)
{
  return p >= PERIOD_MIN && p < PERIOD_MAX;
}

/* Stop firing and wait for a clean run of mains periods */
static void lose_sync(void)
{
  gate_off();
  ev_idx = 4;
  TIM14->CCR1 = 0xFFFF;
  period_avg = 0;
  acc_sum = 0;
  acc_n = 0;
  zc_state = zc_resync;
}

static void zc_acquire(uint16_t period)
{
  if (!period_valid(period))
  {
    lose_sync();
    return;
  }
  acc_sum += period;
  if (++acc_n >= ACQUIRE_COUNT)
  {
    period_avg = (uint16_t)(acc_sum / ACQUIRE_COUNT);
    acc_sum = 0;
    acc_n = 0;
    zc_state = zc_run;
  }
}

static void zc_resync(uint16_t period)
{
  if (!period_valid(period))
  {
    acc_n = 0;
    return;
  }
  if (++acc_n >= RESYNC_COUNT)
  {
    acc_n = 0;
    acc_sum = 0;
    zc_state = zc_acquire;
  }
}

static void zc_run(uint16_t period)
{
  /* Glitch rejection */
  if (period < period_avg / 2)
    return;

  if (table_pending)
  {
    active ^= 1;
    table_pending = false;
  }

  const gate_table_t *t = &tables[active];
  gate_off();
  if (t->fire)
  {
    ev_idx = 0;
    if (t->ev[0] == 0)
    {
      gate_on();
      ev_idx = 1;
    }
    TIM14->CCR1 = t->ev[ev_idx];
  }
  else
  {
    ev_idx = 4;
    TIM14->CCR1 = 0xFFFF;
  }

  acc_sum += period;
  if (++acc_n >= RUN_AVG_COUNT)
  {
    uint16_t avg = (uint16_t)(acc_sum / RUN_AVG_COUNT);
    if (period_valid(avg))
      period_avg = avg;
    acc_sum = 0;
    acc_n = 0;
  }
}

void EXTI4_15_IRQHandler(void)
{
  if (EXTI->PR & (1u << ZC_PIN))
  {
    EXTI->PR = 1u << ZC_PIN;
    uint16_t period = (uint16_t)TIM14->CNT;
    TIM14->CNT = 0;
    TIM14->SR = (uint16_t)~TIM_SR_CC1IF;
    zc_state(period);
  }
}

void TIM14_IRQHandler(void)
{
  TIM14->SR = (uint16_t)~TIM_SR_CC1IF;

  if (ev_idx >= 4)
    return;

  /* Even events turn the gate on, odd events release it */
  if (ev_idx & 1)
    gate_off();
  else
    gate_on();

  if (++ev_idx < 4)
    TIM14->CCR1 = tables[active].ev[ev_idx];
  else
    TIM14->CCR1 = 0xFFFF;
}

void triac_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM14EN;
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

  /* PB3 gate: push-pull, pull-up, idle high (off) */
  gate_off();
  gpio_pull(GATE_PORT, GATE_PIN, GPIO_PULL_UP);
  gpio_speed_high(GATE_PORT, GATE_PIN);
  gpio_mode(GATE_PORT, GATE_PIN, GPIO_MODE_OUT);

  /* PB5 zero cross: input, pull-up, EXTI5 rising edge */
  gpio_mode(ZC_PORT, ZC_PIN, GPIO_MODE_IN);
  gpio_pull(ZC_PORT, ZC_PIN, GPIO_PULL_UP);
  SYSCFG->EXTICR[1] = (SYSCFG->EXTICR[1] & ~(0xFu << 4)) | (1u << 4);
  EXTI->RTSR |= 1u << ZC_PIN;
  EXTI->IMR |= 1u << ZC_PIN;

  /* TIM14: 10 us tick, CC1 output compare (frozen) with interrupt */
  TIM14->PSC = 479;
  TIM14->ARR = 0xFFFF;
  TIM14->CCR1 = 0xFFFF;
  TIM14->EGR = TIM_EGR_UG;
  TIM14->SR = 0;
  TIM14->DIER = TIM_DIER_CC1IE;
  TIM14->CR1 = TIM_CR1_CEN;

  /* Gate events must not be delayed by anything else */
  NVIC_SetPriority(TIM14_IRQn, 0);
  NVIC_SetPriority(EXTI4_15_IRQn, 1);
  NVIC_EnableIRQ(TIM14_IRQn);
  NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void triac_set(bool on, uint8_t fade_mode)
{
  target = on ? TRIAC_LEVEL_MAX : 0;
  fade = fade_mode;
}

bool triac_done(void)
{
  return level == target;
}

uint16_t triac_period(void)
{
  return period_avg;
}

void triac_poll(void)
{
  uint16_t period = period_avg;

  /* One level step per mains cycle: only after the ISR took the last table */
  if (period == 0 || table_pending)
    return;

  if (level != target)
  {
    uint16_t step = triac_fade_step(fade);
    if (level < target)
      level = (uint16_t)(target - level <= step ? target : level + step);
    else
      level = (uint16_t)(level - target <= step ? target : level - step);
    level_dirty = true;
  }

  static uint16_t last_period;
  if (!level_dirty && period == last_period)
    return;

  triac_gate_table(&tables[active ^ 1], period, level);
  last_period = period;
  level_dirty = false;
  table_pending = true;
}
