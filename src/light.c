#include "light.h"

#include "tables.h"

#define HYSTERESIS_COUNT  5

static uint8_t level;
static uint8_t dark_threshold = 100;
static uint8_t run;
static bool dark;
static uint8_t span_min = 100, span_max;

void light_init(void)
{
  level = 0;
  run = 0;
  dark = false;
}

void light_set_dark_threshold(uint8_t threshold)
{
  dark_threshold = threshold;
}

void light_cb(const uint16_t *buf, uint16_t n)
{
  uint16_t x = (uint16_t)(1023 - (buf[n - 1] & 0x3FF));

  level = light_level_table[x];

  if (level < span_min)
    span_min = level;
  if (level > span_max)
    span_max = level;

  /* Flip the dark flag after 5 consecutive readings on the other side */
  bool is_dark = level <= dark_threshold;
  if (is_dark != dark)
  {
    if (++run >= HYSTERESIS_COUNT)
    {
      dark = is_dark;
      run = 0;
    }
  }
  else
  {
    run = 0;
  }
}

uint8_t light_level(void)
{
  return level;
}

bool light_dark(void)
{
  return dark;
}

void light_reset_span(void)
{
  span_min = 100;
  span_max = 0;
}

uint8_t light_span(void)
{
  return span_max > span_min ? (uint8_t)(span_max - span_min) : 0;
}
