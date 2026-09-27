/*
 * OTA image header at image offset 0xC0, directly after the vector table.
 * tools/mkimage.py fills in length and crc after linking; the CRC-32 covers
 * the whole image with the crc field taken as zero.
 */
#ifndef IMAGE_HEADER_H
#define IMAGE_HEADER_H

#include <stdint.h>

/* Tuya product key, also stored at image offset 0x1000 (0x08002000) */
#define PRODUCT_KEY          "urlxtrtlq9fjkv2d"
#define PRODUCT_KEY_LEN      16
#define PRODUCT_KEY_OFFSET   0x1000u

#define IMAGE_MAGIC          0x53524950u   /* "PIRS" */
#define IMAGE_HEADER_OFFSET  0xC0u
#define IMAGE_MAX_SIZE       0x6FFCu

extern const char product_key[PRODUCT_KEY_LEN];

typedef struct {
  uint32_t magic;
  uint32_t length;
  uint32_t crc;
} image_header_t;

#endif
