#include "image_header.h"

__attribute__((section(".image_header"), used))
const image_header_t image_header = {
  .magic = IMAGE_MAGIC,
  .length = 0,   /* patched by tools/mkimage.py */
  .crc = 0,      /* patched by tools/mkimage.py */
};
