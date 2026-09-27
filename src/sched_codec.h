/* Schedule records: DP payload, flash words and matching (spec §13.2-13.4) */
#ifndef SCHED_CODEC_H
#define SCHED_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#define SCHED_SLOTS        7
#define SCHED_ENTRIES      4
#define SCHED_PAYLOAD_MAX  (4 + 9 * SCHED_ENTRIES)
#define SCHED_WORDS_MAX    (SCHED_SLOTS * (1 + 2 * SCHED_ENTRIES))

/* mode */
#define SCHED_MODE_ON_AT_START  1
#define SCHED_MODE_PIR          2
/* off_mode */
#define SCHED_OFF_AT_END        1
#define SCHED_OFF_AUTO          2

typedef struct {
  uint16_t start_min;   /* minutes since midnight */
  uint16_t end_min;     /* end < start: overnight */
  uint16_t autooff_s;
  uint8_t mode;
  uint8_t off_mode;
  uint8_t preset;       /* light gate: dark threshold DP 107 / 123 / 124 */
} sched_entry_t;

typedef struct {
  sched_entry_t e[SCHED_ENTRIES];
  uint8_t hdr_id;
  uint8_t count;
  uint8_t enabled;
  uint8_t weekday_mask; /* bit n = day n, 0 = Sunday */
} sched_t;

extern const sched_t sched_default;

/* DP 116-122 binary payload <-> record; a payload < 4 bytes disables the slot */
void sched_decode(const uint8_t *p, uint16_t len, sched_t *s);
uint16_t sched_encode(const sched_t *s, uint8_t *p);

/* Flash slot 1 layout */
uint32_t sched_to_words(const sched_t s[SCHED_SLOTS], uint32_t *w);
bool sched_from_words(const uint32_t *w, uint32_t count, sched_t s[SCHED_SLOTS]);

/*
 * First enabled entry active at sow (seconds of week, 0 = Sunday 00:00),
 * or NULL. Overnight entries match from start on their weekday until end on
 * the next day.
 */
const sched_entry_t *sched_match(const sched_t s[SCHED_SLOTS], uint32_t sow);

#endif
