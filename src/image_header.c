#include "image_header.h"

__attribute__((section(".image_header"), used))
const image_header_t image_header = {
  .magic = IMAGE_MAGIC,
  .length = 0,   /* patched by tools/mkimage.py */
  .crc = 0,      /* patched by tools/mkimage.py */
};

/* Exactly 16 bytes, no terminator: the stock OTA code compares these */
__attribute__((section(".product_key"), used))
const char product_key[PRODUCT_KEY_LEN] = PRODUCT_KEY;
