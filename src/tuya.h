/* Tuya MCU serial protocol link layer on USART1 (spec §8) */
#ifndef TUYA_H
#define TUYA_H

#include <stdbool.h>
#include <stdint.h>

/* Commands */
#define TUYA_CMD_HEARTBEAT     0x00
#define TUYA_CMD_PRODUCT_INFO  0x01
#define TUYA_CMD_WORK_MODE     0x02
#define TUYA_CMD_WIFI_STATUS   0x03
#define TUYA_CMD_RESET_WIFI    0x04
#define TUYA_CMD_PAIR_MODE     0x05
#define TUYA_CMD_DP_COMMAND    0x06
#define TUYA_CMD_DP_REPORT     0x07
#define TUYA_CMD_DP_QUERY      0x08
#define TUYA_CMD_OTA_START     0x0A
#define TUYA_CMD_OTA_DATA      0x0B
#define TUYA_CMD_GMT_TIME      0x0C
#define TUYA_CMD_WIFI_TEST     0x0E
#define TUYA_CMD_LOCAL_TIME    0x1C
#define TUYA_CMD_SIGNAL        0x2B

/* Pending request status */
#define TUYA_REQ_WAITING   0x01
#define TUYA_REQ_RECEIVED  0x10
#define TUYA_REQ_IDLE      0x1F

typedef struct {
  void (*query_all)(void);
  void (*dp_cmd)(const uint8_t *data, uint16_t len);
  void (*wifi_status)(uint8_t status);
  void (*ota_start)(uint32_t size);
  void (*ota_data)(const uint8_t *data, uint16_t len, uint32_t offset);
  /* Data of a response to one of our requests (0x05/0x0C/0x0E/0x1C/0x2B) */
  void (*response)(uint8_t cmd, const uint8_t *data, uint16_t len);
} tuya_callbacks_t;

void tuya_init(void);
void tuya_set_callbacks(const tuya_callbacks_t *cb);
void tuya_poll(void);

/* Queue a frame; false if a frame is still being sent */
bool tuya_send(uint8_t cmd, const uint8_t *data, uint16_t len);

/* Queue a request and arm the 10 s pending-response record */
bool tuya_request(uint8_t cmd, const uint8_t *data, uint16_t len);
uint8_t tuya_request_status(void);

/* Module alive, nothing unread and nothing being sent */
bool tuya_link_idle(void);
bool tuya_module_alive(void);

/* Pulse the module reset line low for 100 ms (blocking) */
void tuya_module_reset(void);

#endif
