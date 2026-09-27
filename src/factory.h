/* Factory test, entered by holding PF7 at power-up (spec §14.6) */
#ifndef FACTORY_H
#define FACTORY_H

#include <stdint.h>

void factory_start(void);
void factory_poll(void);

/* Response data of the Wi-Fi test command 0x0E */
void factory_wifi_result(const uint8_t *data, uint16_t len);

#endif
