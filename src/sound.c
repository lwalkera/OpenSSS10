#include "sound.h"

#include "tables.h"

static uint8_t threshold = 65;
static uint8_t level;
static uint8_t peak;
static bool trigger;

void sound_init(void)
{
  level = 0;
  peak = 0;
  trigger = false;
}

void sound_set_threshold(uint8_t t)
{
  threshold = t < 20 ? 20 : t > 100 ? 100 : t;
}

void sound_cb(const uint16_t *buf, uint16_t n)
{
  uint32_t sum = 0, dev_sum = 0, maxdev = 0;

  for (uint16_t i = 0; i < n; i++)
    sum += buf[i];
  uint32_t mean = sum / n;

  for (uint16_t i = 0; i < n; i++)
  {
    uint32_t d = buf[i] > mean ? buf[i] - mean : mean - buf[i];
    dev_sum += d;
    if (d > maxdev)
      maxdev = d;
  }

  uint32_t avgdev = dev_sum / n;
  level = avgdev < SOUND_TABLE_LEN ? sound_level_table[avgdev] : 100;
  if (level > peak)
    peak = level;

  if (threshold >= 100)
    trigger = false;
  else
    trigger = maxdev > 500 || level >= threshold;
}

uint8_t sound_level(void)
{
  return level;
}

bool sound_triggered(void)
{
  return trigger;
}

uint8_t sound_take_peak(void)
{
  uint8_t p = peak;

  peak = 0;
  return p;
}
