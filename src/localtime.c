#include "localtime.h"

#include "timebase.h"
#include "tuya.h"

#define SECONDS_PER_WEEK   604800u
#define REQUEST_SYNCED_S   60
#define REQUEST_UNSYNCED_S 3

static bool synced;
static uint32_t sow;
static uint32_t last_sec;
static sw_timer_t request_timer;

void localtime_init(void)
{
  synced = false;
  sow = 0;
  last_sec = clock_seconds();
  timer_set_s(&request_timer, REQUEST_UNSYNCED_S);
}

void time_poll(void)
{
  uint32_t sec = clock_seconds();

  while (last_sec != sec)
  {
    last_sec++;
    if (++sow >= SECONDS_PER_WEEK)
      sow = 0;
  }

  /* Don't displace another request's pending-response record */
  if (timer_expired(&request_timer) && tuya_link_idle() &&
      tuya_request_status() != TUYA_REQ_WAITING)
  {
    if (tuya_request(TUYA_CMD_LOCAL_TIME, 0, 0))
      timer_set_s(&request_timer, synced ? REQUEST_SYNCED_S : REQUEST_UNSYNCED_S);
  }
}

void localtime_response(const uint8_t *d, uint16_t len)
{
  /* ok, year, month, day, hour, min, sec, weekday (1..7, 7 = Sunday) */
  if (len < 8 || d[0] != 1)
    return;
  if (d[4] > 23 || d[5] > 59 || d[6] > 59 || d[7] < 1 || d[7] > 7)
    return;

  sow = (uint32_t)(d[7] % 7) * 86400u + d[4] * 3600u + d[5] * 60u + d[6];
  synced = true;
}

bool localtime_synced(void)
{
  return synced;
}

uint32_t localtime_sow(void)
{
  return sow;
}

uint16_t localtime_minute_of_day(void)
{
  return (uint16_t)((sow % 86400u) / 60u);
}
