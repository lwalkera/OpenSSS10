/*
 * "NELE" flash record format (spec §13.1); hardware independent.
 *
 *   u32 magic = 0x454C454E
 *   u32 count
 *   u32 data[count]
 *   u32 checksum = magic + count + sum(data)
 */
#ifndef RECORD_H
#define RECORD_H

#include <stdbool.h>
#include <stdint.h>

#define RECORD_MAGIC     0x454C454Eu
#define RECORD_OVERHEAD  3u          /* magic, count, checksum */

uint32_t record_checksum(const uint32_t *data, uint32_t count);

/*
 * Validate a record image of max_words words. On success returns true and
 * sets *data and *count to the payload.
 */
bool record_parse(const uint32_t *words, uint32_t max_words, const uint32_t **data,
                  uint32_t *count);

#endif
