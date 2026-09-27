#include "tuya_frame.h"

#include <string.h>

uint8_t tuya_checksum(const uint8_t *buf, uint16_t len)
{
  uint8_t sum = 0;

  while (len--)
    sum = (uint8_t)(sum + *buf++);
  return sum;
}

uint16_t tuya_frame_build(uint8_t *buf, uint8_t cmd, const uint8_t *data, uint16_t len)
{
  if (len > TUYA_MAX_DATA)
    return 0;

  buf[0] = 0x55;
  buf[1] = 0xAA;
  buf[2] = TUYA_TX_VERSION;
  buf[3] = cmd;
  buf[4] = (uint8_t)(len >> 8);
  buf[5] = (uint8_t)len;
  if (len)
    memcpy(&buf[TUYA_HEADER_LEN], data, len);
  buf[TUYA_HEADER_LEN + len] = tuya_checksum(buf, (uint16_t)(TUYA_HEADER_LEN + len));
  return (uint16_t)(TUYA_HEADER_LEN + len + 1);
}

bool tuya_frame_valid(const uint8_t *buf, uint16_t len)
{
  if (len < TUYA_HEADER_LEN + 1 || buf[0] != 0x55 || buf[1] != 0xAA)
    return false;
  if (tuya_frame_data_len(buf) + TUYA_HEADER_LEN + 1u != len)
    return false;
  return tuya_checksum(buf, (uint16_t)(len - 1)) == buf[len - 1];
}
