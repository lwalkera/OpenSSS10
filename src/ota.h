/*
 * Tuya OTA into the staging area for the stock bootloader (spec §8.6).
 * The new image goes to 0x08008000; the install marker 0x4E454C45 at
 * 0x0800EFFC is only written if the image header and CRC-32 check out.
 */
#ifndef OTA_H
#define OTA_H

#include <stdint.h>

void ota_init(void);
void ota_start(uint32_t size);
void ota_data(const uint8_t *data, uint16_t len, uint32_t offset);
void ota_poll(void);

#endif
