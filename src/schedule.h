/* Schedule slots and evaluation (spec §13.2-13.4) */
#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>

#include "sched_codec.h"

extern sched_t schedules[SCHED_SLOTS];

void schedule_load_defaults(void);

/* DP 116-122 string write; returns false if the base64 is invalid */
bool schedule_set_from_dp(unsigned slot, const uint8_t *text, uint16_t len);

/* Flash slot 1 */
bool schedule_load(void);
bool schedule_save(void);

/* Re-evaluate on minute boundaries; call every loop */
void schedule_poll(void);

/* Evaluate again on the next poll (time sync, schedule change, unpairing) */
void schedule_reevaluate(void);

/* Active record: the default record while unpaired, else the matching entry or NULL */
const sched_entry_t *schedule_active(void);

#endif
