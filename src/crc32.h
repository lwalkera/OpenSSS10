/* CRC-32 (IEEE 802.3, as zlib.crc32) */
#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

#define CRC32_INIT  0xFFFFFFFFu

/* Running update; finish with ~crc */
uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n);

static inline uint32_t crc32(const uint8_t *p, uint32_t n)
{
  return ~crc32_update(CRC32_INIT, p, n);
}

#endif
