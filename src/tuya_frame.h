/* Tuya MCU serial frame encoding (spec §8.1); hardware independent */
#ifndef TUYA_FRAME_H
#define TUYA_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#define TUYA_HEADER_LEN   6      /* 55 AA ver cmd len_hi len_lo */
#define TUYA_MAX_DATA     260
#define TUYA_MAX_FRAME    (TUYA_MAX_DATA + TUYA_HEADER_LEN + 1)
#define TUYA_TX_VERSION   0x03

uint8_t tuya_checksum(const uint8_t *buf, uint16_t len);

/* Build a frame into buf (TUYA_MAX_FRAME bytes); returns its length, 0 if too long */
uint16_t tuya_frame_build(uint8_t *buf, uint8_t cmd, const uint8_t *data, uint16_t len);

/* Validate a complete received frame (header, length and checksum) */
bool tuya_frame_valid(const uint8_t *buf, uint16_t len);

static inline uint8_t tuya_frame_cmd(const uint8_t *buf)
{
  return buf[3];
}

static inline uint16_t tuya_frame_data_len(const uint8_t *buf)
{
  return (uint16_t)((buf[4] << 8) | buf[5]);
}

#endif
