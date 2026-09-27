/* Local time from the Tuya module (spec §15) */
#ifndef LOCALTIME_H
#define LOCALTIME_H

#include <stdbool.h>
#include <stdint.h>

void localtime_init(void);
void time_poll(void);

/* Response data of command 0x1C */
void localtime_response(const uint8_t *data, uint16_t len);

bool localtime_synced(void);
uint32_t localtime_sow(void);                /* seconds of week, 0 = Sunday 00:00 */
uint16_t localtime_minute_of_day(void);

#endif
