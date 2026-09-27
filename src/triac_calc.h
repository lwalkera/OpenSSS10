/* TRIAC gate-event timing (spec §9.2); hardware independent */
#ifndef TRIAC_CALC_H
#define TRIAC_CALC_H

#include <stdbool.h>
#include <stdint.h>

#define TRIAC_LEVEL_MAX  1000

typedef struct {
  uint16_t ev[4];   /* gate on, off, on, off - in 10 us ticks after zero cross */
  bool fire;        /* false: keep the gate off for the whole cycle */
} gate_table_t;

/*
 * period: mains period in 10 us ticks (one zero-cross edge per full cycle).
 * The gate is held on from the firing angle to ~165 deg of each half cycle.
 */
static inline void triac_gate_table(gate_table_t *t, uint16_t period, uint16_t level)
{
  uint32_t half = period / 2u;
  uint32_t off30 = half * 30u / 180u;
  uint32_t t_on, t_off = half - off30 / 2u;

  if (level >= TRIAC_LEVEL_MAX)
    t_on = 0;
  else if (level == 0)
    t_on = half;
  else
    t_on = half - ((uint32_t)level * (half - off30) / TRIAC_LEVEL_MAX + off30);

  t->fire = level > 0;
  t->ev[0] = (uint16_t)t_on;
  t->ev[1] = (uint16_t)t_off;
  t->ev[2] = (uint16_t)(t_on + half);
  t->ev[3] = (uint16_t)(t_off + half);
}

/* Level step per mains cycle for fade modes 0..3 (spec §9.3); 0 = jump */
static inline uint16_t triac_fade_step(uint8_t mode)
{
  static const uint16_t steps[4] = { TRIAC_LEVEL_MAX, 25, 3, 1 };
  return steps[mode > 3 ? 3 : mode];
}

#endif
