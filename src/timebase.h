/* Millisecond clock and software timers (spec §5.1, §6.1) */
#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t sec;
  uint16_t ms;
} sw_timer_t;

void timebase_init(void);

/* Advance the global clock from TIM6; call once per main-loop pass */
void clock_update(void);

/* Current time */
sw_timer_t clock_now(void);
uint32_t clock_seconds(void);

void timer_set_ms(sw_timer_t *t, uint32_t ms);
void timer_set_s(sw_timer_t *t, uint32_t s);
bool timer_expired(const sw_timer_t *t);

/* Blocking delays; delay_ms uses TIM6, delay_cycles uses the free-running SysTick */
void delay_ms(uint32_t ms);
void delay_cycles(uint32_t cycles);

#define CYCLES_PER_US  48u
#define delay_us(us)   delay_cycles((uint32_t)(us) * CYCLES_PER_US)

#endif
