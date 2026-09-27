/* Settings storage in the last 4 KB of flash (spec §13.1) */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stdint.h>

enum {
  SLOT_SETTINGS = 0,   /* 0x0800F000-0x0800F200 */
  SLOT_SCHEDULES = 1,  /* 0x0800F200-0x0800FA00 */
  SLOT_TAG = 2,        /* 0x0800FE00-0x08010000 */
  SLOT_COUNT
};

/* Copy a valid record's payload into out (max words); returns false if invalid */
bool storage_read(int slot, uint32_t *out, uint32_t max, uint32_t *count);

/* Erase the slot and write a record with the given payload */
bool storage_write(int slot, const uint32_t *data, uint32_t count);

/* Low-level helpers shared with the OTA code */
bool flash_erase_range(uint32_t start, uint32_t end);
bool flash_write_words(uint32_t addr, const uint32_t *data, uint32_t count);

#define FLASH_PAGE_SIZE  512u

#endif
