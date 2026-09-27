#include "schedule.h"

#include <stddef.h>
#include <string.h>

#include "app.h"
#include "base64.h"
#include "dp.h"
#include "light.h"
#include "localtime.h"
#include "pir.h"
#include "storage.h"

sched_t schedules[SCHED_SLOTS];

static const sched_entry_t *active;
static uint16_t last_minute = 0xFFFF;
static bool force = true;
static bool started;

void schedule_load_defaults(void)
{
  memset(schedules, 0, sizeof(schedules));
  schedules[0] = sched_default;
  for (int i = 1; i < SCHED_SLOTS; i++)
    schedules[i].hdr_id = (uint8_t)i;
  force = true;
}

bool schedule_set_from_dp(unsigned slot, const uint8_t *text, uint16_t len)
{
  uint8_t payload[SCHED_PAYLOAD_MAX];
  int n = base64_decode((const char *)text, len, payload, sizeof(payload));

  if (slot >= SCHED_SLOTS || n < 0)
    return false;
  sched_decode(payload, (uint16_t)n, &schedules[slot]);
  force = true;
  return true;
}

bool schedule_load(void)
{
  uint32_t words[SCHED_WORDS_MAX];
  uint32_t count;

  if (!storage_read(SLOT_SCHEDULES, words, SCHED_WORDS_MAX, &count) ||
      !sched_from_words(words, count, schedules))
    return false;
  force = true;
  return true;
}

bool schedule_save(void)
{
  uint32_t words[SCHED_WORDS_MAX];
  uint32_t count = sched_to_words(schedules, words);

  return storage_write(SLOT_SCHEDULES, words, count);
}

/*
 * The light preset selects one of the configurable dark thresholds
 * DP 107 / 123 / 124 (defaults 10 / 20 / 100), capped at 100. As in the stock
 * firmware, an out-of-range preset leaves the threshold unchanged.
 */
static void apply_preset(uint8_t preset)
{
  int32_t t;

  switch (preset)
  {
  case 0:  t = dp.dp107; break;
  case 1:  t = dp.dp123; break;
  case 2:  t = dp.dp124; break;
  default: return;
  }
  light_set_dark_threshold((uint8_t)(t < 0 ? 0 : t > 100 ? 100 : t));
}

void schedule_reevaluate(void)
{
  force = true;
}

const sched_entry_t *schedule_active(void)
{
  return active;
}

void schedule_poll(void)
{
  /* Like the stock firmware, start once the PIR has warmed up and, when
     paired, the time is known */
  if (!started)
  {
    if (!pir_ready() || (dp.paired && !localtime_synced()))
      return;
    started = true;
  }

  if (!dp.paired)
  {
    /* No valid time without the cloud: use the built-in PIR night-light */
    active = &sched_default.e[0];
    apply_preset(active->preset);
    last_minute = 0xFFFF;
    return;
  }

  if (!localtime_synced())
  {
    active = NULL;
    return;
  }

  uint16_t minute = localtime_minute_of_day();
  bool new_minute = minute != last_minute;
  if (!new_minute && !force)
    return;

  const sched_entry_t *prev = active;
  const sched_entry_t *e = sched_match(schedules, localtime_sow());

  if (new_minute && prev && prev != &sched_default.e[0] && e != prev &&
      minute == prev->end_min && prev->off_mode == SCHED_OFF_AT_END)
    app_switch(false, SRC_TIMER);

  if (e)
  {
    apply_preset(e->preset);
    if (new_minute && e->mode == SCHED_MODE_ON_AT_START && minute == e->start_min)
      app_switch(true, SRC_TIMER);
  }

  active = e;
  last_minute = minute;
  force = false;
}
