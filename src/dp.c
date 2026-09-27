#include "dp.h"

#include <string.h>

#include "base64.h"
#include "schedule.h"
#include "timebase.h"
#include "tuya.h"
#include "tuya_frame.h"

#define REPORT_GAP_MS  150

dp_state_t dp;

static uint32_t dirty;
static uint8_t report_state;
static sw_timer_t report_timer;
static uint8_t report_buf[TUYA_MAX_DATA];

void dp_load_defaults(void)
{
  dp.sw = false;
  dp.countdown = 0;
  dp.pir_sens = 2;
  dp.holdoff_s = 10;
  dp.lvl_white = 100;
  dp.lvl_cyan = 100;
  dp.lvl_status = 100;
  dp.fade_in = 0;
  dp.fade_out = 0;
  dp.sound_thr = 65;
  dp.dp107 = 10;
  dp.sw_after_countdown = false;
  dp.test_mode = 0;
  dp.dp123 = 20;
  dp.dp124 = 100;
  dp.ambient = true;
}

void dp_report(unsigned bit)
{
  dirty |= 1u << bit;
}

void dp_report_all(void)
{
  dirty = RB_ALL;
}

static uint16_t put_header(uint8_t *p, uint8_t id, uint8_t type, uint16_t len)
{
  p[0] = id;
  p[1] = type;
  p[2] = (uint8_t)(len >> 8);
  p[3] = (uint8_t)len;
  return 4;
}

static uint16_t put_u8(uint8_t *p, uint8_t id, uint8_t type, uint8_t v)
{
  uint16_t n = put_header(p, id, type, 1);
  p[n] = v;
  return (uint16_t)(n + 1);
}

static uint16_t put_value(uint8_t *p, uint8_t id, int32_t v)
{
  uint16_t n = put_header(p, id, DP_TYPE_VALUE, 4);
  uint32_t u = (uint32_t)v;
  p[n] = (uint8_t)(u >> 24);
  p[n + 1] = (uint8_t)(u >> 16);
  p[n + 2] = (uint8_t)(u >> 8);
  p[n + 3] = (uint8_t)u;
  return (uint16_t)(n + 4);
}

static uint16_t put_string(uint8_t *p, uint8_t id, const char *s, uint16_t len)
{
  uint16_t n = put_header(p, id, DP_TYPE_STRING, len);
  memcpy(&p[n], s, len);
  return (uint16_t)(n + len);
}

static uint16_t put_schedule(uint8_t *p, unsigned slot)
{
  uint8_t payload[SCHED_PAYLOAD_MAX];
  char text[BASE64_ENCODED_LEN(SCHED_PAYLOAD_MAX)];
  uint16_t len = sched_encode(&schedules[slot], payload);
  uint16_t tlen = base64_encode(payload, len, text);

  if (4u + tlen > TUYA_MAX_DATA)
    return 0;
  return put_string(p, (uint8_t)(DP_SCHED_FIRST + slot), text, tlen);
}

/* Encode the DP(s) for one report bit; returns the payload length */
static uint16_t encode_bit(unsigned bit, uint8_t *p)
{
  switch (bit)
  {
  case RB_SWITCH:       return put_u8(p, DP_SWITCH, DP_TYPE_BOOL, dp.sw);
  case RB_COUNTDOWN:    return put_value(p, DP_COUNTDOWN, (int32_t)dp.countdown);
  case RB_PIR_SENS:     return put_u8(p, DP_PIR_SENS, DP_TYPE_ENUM, dp.pir_sens);
  case RB_HOLDOFF:      return put_value(p, DP_HOLDOFF, (int32_t)dp.holdoff_s);
  case RB_LVL_WHITE:    return put_value(p, DP_LVL_WHITE, dp.lvl_white);
  case RB_LVL_CYAN:     return put_value(p, DP_LVL_CYAN, dp.lvl_cyan);
  case RB_FADE_IN:      return put_u8(p, DP_FADE_IN, DP_TYPE_ENUM, dp.fade_in);
  case RB_FADE_OUT:     return put_u8(p, DP_FADE_OUT, DP_TYPE_ENUM, dp.fade_out);
  case RB_LVL_STATUS:   return put_value(p, DP_LVL_STATUS, dp.lvl_status);
  case RB_SOUND_THR:    return put_value(p, DP_SOUND_THR, dp.sound_thr);
  case RB_107:          return put_value(p, DP_107, dp.dp107);
  case RB_TAG_VERSION:
  {
    uint16_t n = put_string(p, DP_TAG, dp.tag, (uint16_t)strlen(dp.tag));
    return (uint16_t)(n + put_string(&p[n], DP_FW_VERSION, FW_VERSION,
                                     sizeof(FW_VERSION) - 1));
  }
  case RB_SW_AFTER_CD:  return put_u8(p, DP_SW_AFTER_CD, DP_TYPE_BOOL, dp.sw_after_countdown);
  case RB_111:          return put_value(p, DP_111, dp.dp111);
  case RB_123:          return put_value(p, DP_123, dp.dp123);
  case RB_124:          return put_value(p, DP_124, dp.dp124);
#ifdef FEATURE_SENSOR_TEST
  case RB_TEST_MODE:    return put_u8(p, DP_TEST_MODE, DP_TYPE_ENUM, dp.test_mode);
  case RB_LIGHT:        return put_value(p, DP_LIGHT, dp.light);
  case RB_SOUND_PEAK:   return put_value(p, DP_SOUND_PEAK, dp.sound_peak);
#endif
  case RB_AMBIENT:      return put_u8(p, DP_AMBIENT, DP_TYPE_BOOL, dp.ambient);
  default:
    if (bit >= RB_SCHED_FIRST && bit < RB_SCHED_FIRST + SCHED_SLOTS)
      return put_schedule(p, bit - RB_SCHED_FIRST);
    return 0;
  }
}

void dp_report_poll(void)
{
  switch (report_state)
  {
  case 0:
    if (dirty && tuya_link_idle())
    {
      unsigned bit = 0;
      while (!(dirty & (1u << bit)))
        bit++;
      dirty &= ~(1u << bit);

      uint16_t len = encode_bit(bit, report_buf);
      if (len && tuya_send(TUYA_CMD_DP_REPORT, report_buf, len))
        report_state = 1;
    }
    break;

  case 1:
    if (tuya_link_idle())
    {
      timer_set_ms(&report_timer, REPORT_GAP_MS);
      report_state = 2;
    }
    break;

  default:
    if (timer_expired(&report_timer))
      report_state = 0;
    break;
  }
}

void dp_parse(const uint8_t *data, uint16_t len, dp_record_fn fn)
{
  uint16_t i = 0;

  while (i + 4 <= len)
  {
    uint8_t id = data[i];
    uint8_t type = data[i + 1];
    uint16_t vlen = (uint16_t)((data[i + 2] << 8) | data[i + 3]);

    if (i + 4u + vlen > len)
      break;
    fn(id, type, &data[i + 4], vlen);
    i = (uint16_t)(i + 4 + vlen);
  }
}
