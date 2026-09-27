/* Tuya data points: values, encoding and report pacing (spec §12) */
#ifndef DP_H
#define DP_H

#include <stdbool.h>
#include <stdint.h>

/* DP types */
#define DP_TYPE_BOOL    1
#define DP_TYPE_VALUE   2
#define DP_TYPE_STRING  3
#define DP_TYPE_ENUM    4

/* DP ids */
#define DP_SWITCH          1
#define DP_COUNTDOWN       7
#define DP_PIR_SENS        48
#define DP_HOLDOFF         50
#define DP_LVL_WHITE       101
#define DP_LVL_CYAN        102
#define DP_FADE_IN         103
#define DP_FADE_OUT        104
#define DP_LVL_STATUS      105
#define DP_SOUND_THR       106
#define DP_107             107
#define DP_TAG             109
#define DP_SW_AFTER_CD     110
#define DP_111             111
#define DP_REPORT_CD       112
#define DP_FW_VERSION      113
#define DP_TEST_MODE       115
#define DP_SCHED_FIRST     116
#define DP_SCHED_LAST      122
#define DP_123             123
#define DP_124             124
#define DP_LIGHT           125
#define DP_SOUND_PEAK      126
#define DP_AMBIENT         127

/* Report-dirty bits */
enum {
  RB_SWITCH = 0,
  RB_COUNTDOWN = 1,
  RB_PIR_SENS = 2,
  RB_HOLDOFF = 3,
  RB_LVL_WHITE = 4,
  RB_LVL_CYAN = 5,
  RB_FADE_IN = 6,
  RB_FADE_OUT = 7,
  RB_LVL_STATUS = 8,
  RB_SOUND_THR = 9,
  RB_107 = 10,
  RB_TAG_VERSION = 11,   /* DP 109 + DP 113 */
  RB_SW_AFTER_CD = 12,
  RB_111 = 13,
  RB_TEST_MODE = 14,
  RB_SCHED_FIRST = 15,   /* 15..21 = DP 116..122 */
  RB_123 = 22,
  RB_124 = 23,
  RB_LIGHT = 24,
  RB_SOUND_PEAK = 25,
  RB_AMBIENT = 26,
  RB_COUNT = 27
};

#define RB_ALL  ((1u << RB_COUNT) - 1u)

#define FW_VERSION  "2.3.4"
#define TAG_MAX     8

typedef struct {
  bool sw;                  /* 1 */
  uint32_t countdown;       /* 7, seconds */
  uint8_t pir_sens;         /* 48, 0..2 */
  uint32_t holdoff_s;       /* 50 */
  uint8_t lvl_white;        /* 101, 0..100 */
  uint8_t lvl_cyan;         /* 102 */
  uint8_t fade_in;          /* 103, 0..3 */
  uint8_t fade_out;         /* 104, 0..3 */
  uint8_t lvl_status;       /* 105 */
  uint8_t sound_thr;        /* 106, 20..100 */
  int32_t dp107;            /* purpose unknown */
  bool sw_after_countdown;  /* 110 */
  int32_t dp111;            /* purpose unknown, RAM only */
  uint8_t test_mode;        /* 115 */
  int32_t dp123;            /* purpose unknown */
  int32_t dp124;            /* purpose unknown */
  uint8_t light;            /* 125 */
  uint8_t sound_peak;       /* 126 */
  bool ambient;             /* 127 */

  /* Persisted with the settings but not DPs */
  uint32_t settings_w0;     /* settings word 0, purpose unknown */
  bool paired;

  char tag[TAG_MAX + 1];    /* DP 109, flash slot 2 */
} dp_state_t;

extern dp_state_t dp;

void dp_load_defaults(void);

void dp_report(unsigned bit);
void dp_report_all(void);

/* Report pacer: one DP per frame, link idle, then 150 ms gap */
void dp_report_poll(void);

/* Iterate over the DP records of a 0x06 payload */
typedef void (*dp_record_fn)(uint8_t id, uint8_t type, const uint8_t *value, uint16_t len);
void dp_parse(const uint8_t *data, uint16_t len, dp_record_fn fn);

static inline uint32_t dp_value_be32(const uint8_t *v)
{
  return ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
}

#endif
