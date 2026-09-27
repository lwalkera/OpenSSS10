#include "sched_codec.h"

#include <stddef.h>
#include <string.h>

const sched_t sched_default = {
  .e = { { .start_min = 0, .end_min = 23 * 60 + 59, .autooff_s = 60,
           .mode = SCHED_MODE_PIR, .off_mode = SCHED_OFF_AUTO, .preset = 2 } },
  .hdr_id = 0,
  .count = 1,
  .enabled = 1,
  .weekday_mask = 0x7F,
};

void sched_decode(const uint8_t *p, uint16_t len, sched_t *s)
{
  memset(s, 0, sizeof(*s));
  if (len < 4)
    return;

  s->hdr_id = p[0];
  s->enabled = p[2];
  s->weekday_mask = p[3];

  uint16_t n = (uint16_t)((len - 4) / 9);
  if (n > SCHED_ENTRIES)
    n = SCHED_ENTRIES;
  s->count = (uint8_t)n;

  for (uint16_t i = 0; i < n; i++)
  {
    const uint8_t *q = &p[4 + 9 * i];
    sched_entry_t *e = &s->e[i];
    e->start_min = (uint16_t)((q[0] << 8) | q[1]);
    e->end_min = (uint16_t)((q[2] << 8) | q[3]);
    e->mode = q[4];
    e->off_mode = q[5];
    e->preset = q[6];
    e->autooff_s = (uint16_t)((q[7] << 8) | q[8]);
  }
}

uint16_t sched_encode(const sched_t *s, uint8_t *p)
{
  uint16_t len = (uint16_t)(4 + 9 * s->count);

  p[0] = s->hdr_id;
  p[1] = (uint8_t)len;
  p[2] = s->enabled;
  p[3] = s->weekday_mask;
  for (uint8_t i = 0; i < s->count; i++)
  {
    const sched_entry_t *e = &s->e[i];
    uint8_t *q = &p[4 + 9 * i];
    q[0] = (uint8_t)(e->start_min >> 8);
    q[1] = (uint8_t)e->start_min;
    q[2] = (uint8_t)(e->end_min >> 8);
    q[3] = (uint8_t)e->end_min;
    q[4] = e->mode;
    q[5] = e->off_mode;
    q[6] = e->preset;
    q[7] = (uint8_t)(e->autooff_s >> 8);
    q[8] = (uint8_t)e->autooff_s;
  }
  return len;
}

uint32_t sched_to_words(const sched_t s[SCHED_SLOTS], uint32_t *w)
{
  uint32_t n = 0;

  for (int i = 0; i < SCHED_SLOTS; i++)
  {
    const sched_t *r = &s[i];
    uint32_t len = 4u + 9u * r->count;

    w[n++] = ((uint32_t)r->hdr_id << 24) | (len << 16) | ((uint32_t)r->enabled << 8) |
             r->weekday_mask;
    for (uint8_t j = 0; j < r->count; j++)
    {
      const sched_entry_t *e = &r->e[j];
      w[n++] = ((uint32_t)e->start_min << 16) | e->end_min;
      w[n++] = ((uint32_t)e->autooff_s << 16) |
               ((uint32_t)((e->off_mode << 4) | (e->mode & 0x0F)) << 8) | e->preset;
    }
  }
  return n;
}

bool sched_from_words(const uint32_t *w, uint32_t count, sched_t s[SCHED_SLOTS])
{
  uint32_t n = 0;

  memset(s, 0, sizeof(sched_t) * SCHED_SLOTS);
  for (int i = 0; i < SCHED_SLOTS; i++)
  {
    if (n >= count)
      return false;

    uint32_t h = w[n++];
    uint32_t len = (h >> 16) & 0xFF;
    uint32_t entries = len >= 4 ? (len - 4) / 9 : 0;

    if (entries > SCHED_ENTRIES || n + 2 * entries > count)
      return false;

    sched_t *r = &s[i];
    r->hdr_id = (uint8_t)(h >> 24);
    r->enabled = (uint8_t)(h >> 8);
    r->weekday_mask = (uint8_t)h;
    r->count = (uint8_t)entries;
    for (uint32_t j = 0; j < entries; j++)
    {
      uint32_t a = w[n++], b = w[n++];
      sched_entry_t *e = &r->e[j];
      e->start_min = (uint16_t)(a >> 16);
      e->end_min = (uint16_t)a;
      e->autooff_s = (uint16_t)(b >> 16);
      e->off_mode = (uint8_t)((b >> 12) & 0x0F);
      e->mode = (uint8_t)((b >> 8) & 0x0F);
      e->preset = (uint8_t)b;
    }
  }
  return n == count;
}

const sched_entry_t *sched_match(const sched_t s[SCHED_SLOTS], uint32_t sow)
{
  uint32_t day = (sow / 86400u) % 7u;
  uint32_t yesterday = (day + 6u) % 7u;
  uint32_t minute = (sow % 86400u) / 60u;

  for (int i = 0; i < SCHED_SLOTS; i++)
  {
    const sched_t *r = &s[i];
    if (!r->enabled)
      continue;

    for (uint8_t j = 0; j < r->count; j++)
    {
      const sched_entry_t *e = &r->e[j];
      if (e->start_min < e->end_min)
      {
        if ((r->weekday_mask & (1u << day)) && minute >= e->start_min && minute < e->end_min)
          return e;
      }
      else if (e->start_min > e->end_min)
      {
        if (((r->weekday_mask & (1u << day)) && minute >= e->start_min) ||
            ((r->weekday_mask & (1u << yesterday)) && minute < e->end_min))
          return e;
      }
    }
  }
  return NULL;
}
