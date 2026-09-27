#include "ota.h"

#include <stdbool.h>

#include "crc32.h"
#include "ft32f0xx.h"
#include "image_header.h"
#include "storage.h"
#include "timebase.h"

#define STAGING_START    0x08008000u
#define STAGING_END      0x0800F000u
#define MARKER_ADDR      0x0800EFFCu
#define INSTALL_MARKER   0x4E454C45u
#define TIMEOUT_MS       5000
#define RESET_DELAY_MS   500

enum { OTA_IDLE, OTA_RECEIVING, OTA_COMPLETE };

static uint8_t state;
static uint32_t expected;
static uint32_t received;
static sw_timer_t timer;

void ota_init(void)
{
  state = OTA_IDLE;
}

void ota_start(uint32_t size)
{
  state = OTA_IDLE;
  if (size == 0 || size > IMAGE_MAX_SIZE)
    return;
  if (!flash_erase_range(STAGING_START, STAGING_END))
    return;

  expected = size;
  received = 0;
  state = OTA_RECEIVING;
  timer_set_ms(&timer, TIMEOUT_MS);
}

static bool image_valid(void)
{
  const uint8_t *img = (const uint8_t *)STAGING_START;
  const image_header_t *h = (const image_header_t *)(STAGING_START + IMAGE_HEADER_OFFSET);
  uint32_t sp = *(const uint32_t *)STAGING_START;
  static const uint8_t zero[4];

  if ((sp & 0x2FFE0000u) != 0x20000000u)
    return false;
  if (h->magic != IMAGE_MAGIC || h->length != expected)
    return false;

  /* CRC over the image with the crc field taken as zero */
  uint32_t crc_off = IMAGE_HEADER_OFFSET + 8;
  uint32_t crc = crc32_update(CRC32_INIT, img, crc_off);
  crc = crc32_update(crc, zero, 4);
  crc = crc32_update(crc, img + crc_off + 4, expected - crc_off - 4);
  return ~crc == h->crc;
}

void ota_data(const uint8_t *data, uint16_t len, uint32_t offset)
{
  if (state != OTA_RECEIVING || offset != received || len == 0)
    return;
  if (received + len > expected)
    return;

  bool last = received + len == expected;
  if ((len & 3) && !last)
    return;

  /* Program whole words, padding the final partial word with 0xFF */
  for (uint16_t i = 0; i < len; i += 4)
  {
    uint32_t w = 0xFFFFFFFFu;
    for (uint16_t k = 0; k < 4 && i + k < len; k++)
    {
      w &= ~(0xFFu << (8 * k));
      w |= (uint32_t)data[i + k] << (8 * k);
    }
    if (!flash_write_words(STAGING_START + offset + i, &w, 1))
    {
      state = OTA_IDLE;
      return;
    }
  }

  received += len;
  timer_set_ms(&timer, TIMEOUT_MS);

  if (received == expected)
  {
    uint32_t marker = INSTALL_MARKER;
    if (image_valid() && flash_write_words(MARKER_ADDR, &marker, 1))
    {
      state = OTA_COMPLETE;
      timer_set_ms(&timer, RESET_DELAY_MS);
    }
    else
    {
      state = OTA_IDLE;
    }
  }
}

void ota_poll(void)
{
  if (state == OTA_RECEIVING && timer_expired(&timer))
    state = OTA_IDLE;
  else if (state == OTA_COMPLETE && timer_expired(&timer))
    NVIC_SystemReset();   /* the bootloader installs the staged image */
}
