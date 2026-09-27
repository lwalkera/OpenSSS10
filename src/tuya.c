#include "tuya.h"

#include <stddef.h>

#include "board.h"
#include "image_header.h"
#include "timebase.h"
#include "tuya_frame.h"
#include "uart.h"

#define FRAME_TIMEOUT_MS      1000
#define HEARTBEAT_TIMEOUT_S   180
#define REQUEST_TIMEOUT_MS    10000
#define MODULE_RESET_MS       100

static const char product_info[] =
  "{\"p\":\"" PRODUCT_KEY "\",\"v\":\"2.3.4\",\"m\":2,\"mt\":3,\"n\":0,\"low\":0}";

enum { RX_IDLE, RX_SYNC, RX_HEADER, RX_BODY, RX_DISPATCH };

static tuya_callbacks_t cb;

static uint8_t rx_buf[TUYA_MAX_FRAME];
static uint16_t rx_len, rx_total;
static uint8_t rx_state;
static sw_timer_t rx_timer;

static uint8_t tx_buf[TUYA_MAX_FRAME];
static uint16_t tx_len, tx_idx;

static bool alive;
static sw_timer_t heartbeat_timer;

static uint8_t pending_cmd;
static uint8_t pending_status = TUYA_REQ_IDLE;
static sw_timer_t pending_timer;

void tuya_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
  gpio_set(MODRST_PORT, MODRST_PIN);
  gpio_speed_high(MODRST_PORT, MODRST_PIN);
  gpio_mode(MODRST_PORT, MODRST_PIN, GPIO_MODE_OUT);

  rx_state = RX_IDLE;
  tx_len = 0;
  alive = false;
  /* Also reset a module that never sends a heartbeat after MCU boot */
  timer_set_s(&heartbeat_timer, HEARTBEAT_TIMEOUT_S);
}

void tuya_set_callbacks(const tuya_callbacks_t *callbacks)
{
  cb = *callbacks;
}

bool tuya_send(uint8_t cmd, const uint8_t *data, uint16_t len)
{
  if (tx_len != 0)
    return false;
  tx_len = tuya_frame_build(tx_buf, cmd, data, len);
  tx_idx = 0;
  return tx_len != 0;
}

bool tuya_request(uint8_t cmd, const uint8_t *data, uint16_t len)
{
  if (!tuya_send(cmd, data, len))
    return false;
  pending_cmd = cmd;
  pending_status = TUYA_REQ_WAITING;
  timer_set_ms(&pending_timer, REQUEST_TIMEOUT_MS);
  return true;
}

uint8_t tuya_request_status(void)
{
  return pending_status;
}

bool tuya_module_alive(void)
{
  return alive;
}

bool tuya_link_idle(void)
{
  return alive && rx_state == RX_IDLE && uart_avail(UART_TUYA) == 0 && tx_len == 0;
}

void tuya_module_reset(void)
{
  gpio_clear(MODRST_PORT, MODRST_PIN);
  delay_ms(MODULE_RESET_MS);
  gpio_set(MODRST_PORT, MODRST_PIN);
  alive = false;
  timer_set_s(&heartbeat_timer, HEARTBEAT_TIMEOUT_S);
}

static uint32_t be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void dispatch(uint8_t cmd, const uint8_t *data, uint16_t len)
{
  switch (cmd)
  {
  case TUYA_CMD_HEARTBEAT:
  {
    uint8_t was_alive = alive ? 1 : 0;
    tuya_send(TUYA_CMD_HEARTBEAT, &was_alive, 1);
    alive = true;
    timer_set_s(&heartbeat_timer, HEARTBEAT_TIMEOUT_S);
    break;
  }

  case TUYA_CMD_PRODUCT_INFO:
    tuya_send(TUYA_CMD_PRODUCT_INFO, (const uint8_t *)product_info, sizeof(product_info) - 1);
    break;

  case TUYA_CMD_WORK_MODE:
    tuya_send(TUYA_CMD_WORK_MODE, NULL, 0);
    break;

  case TUYA_CMD_WIFI_STATUS:
    if (len >= 1 && cb.wifi_status)
      cb.wifi_status(data[0]);
    tuya_send(TUYA_CMD_WIFI_STATUS, NULL, 0);
    break;

  case TUYA_CMD_PAIR_MODE:
  case TUYA_CMD_GMT_TIME:
  case TUYA_CMD_WIFI_TEST:
  case TUYA_CMD_LOCAL_TIME:
  case TUYA_CMD_SIGNAL:
    if (pending_status == TUYA_REQ_WAITING && cmd == pending_cmd)
    {
      pending_status = TUYA_REQ_RECEIVED;
      pending_cmd = 0;
    }
    if (cb.response)
      cb.response(cmd, data, len);
    break;

  case TUYA_CMD_DP_COMMAND:
    if (cb.dp_cmd)
      cb.dp_cmd(data, len);
    break;

  case TUYA_CMD_DP_QUERY:
    if (cb.query_all)
      cb.query_all();
    break;

  case TUYA_CMD_OTA_START:
  {
    /* Always 256-byte packets */
    uint8_t packet_size = 0;
    if (len >= 4 && cb.ota_start)
      cb.ota_start(be32(data));
    tuya_send(TUYA_CMD_OTA_START, &packet_size, 1);
    break;
  }

  case TUYA_CMD_OTA_DATA:
    if (len >= 4)
    {
      if (cb.ota_data)
        cb.ota_data(data + 4, (uint16_t)(len - 4), be32(data));
      tuya_send(TUYA_CMD_OTA_DATA, NULL, 0);
    }
    break;

  default:
    break;
  }
}

static void resync(void)
{
  uart_flush(UART_TUYA);
  rx_state = RX_IDLE;
}

static void parse(void)
{
  if (rx_state != RX_IDLE && timer_expired(&rx_timer))
  {
    resync();
    return;
  }

  switch (rx_state)
  {
  case RX_IDLE:
    if (uart_avail(UART_TUYA) > 0)
    {
      timer_set_ms(&rx_timer, FRAME_TIMEOUT_MS);
      rx_len = 0;
      rx_state = RX_SYNC;
    }
    break;

  case RX_SYNC:
    if (uart_avail(UART_TUYA) >= 2)
    {
      rx_len = uart_read(UART_TUYA, rx_buf, 2);
      if (rx_buf[0] == 0x55 && rx_buf[1] == 0xAA)
        rx_state = RX_HEADER;
      else
        resync();
    }
    break;

  case RX_HEADER:
    if (uart_avail(UART_TUYA) >= 4)
    {
      rx_len = (uint16_t)(rx_len + uart_read(UART_TUYA, &rx_buf[2], 4));
      rx_total = (uint16_t)(tuya_frame_data_len(rx_buf) + TUYA_HEADER_LEN + 1);
      if (rx_total > TUYA_MAX_FRAME)
        resync();
      else
        rx_state = RX_BODY;
    }
    break;

  case RX_BODY:
    rx_len = (uint16_t)(rx_len + uart_read(UART_TUYA, &rx_buf[rx_len],
                                           (uint16_t)(rx_total - rx_len)));
    if (rx_len == rx_total)
      rx_state = RX_DISPATCH;
    break;

  default:
    /* Replies go through the single TX buffer: wait until it is free */
    if (tx_len == 0)
    {
      if (tuya_frame_valid(rx_buf, rx_len))
        dispatch(tuya_frame_cmd(rx_buf), &rx_buf[TUYA_HEADER_LEN], tuya_frame_data_len(rx_buf));
      rx_state = RX_IDLE;
    }
    break;
  }
}

void tuya_poll(void)
{
  /* Pump the TX frame into the UART ring */
  while (tx_len && tx_idx < tx_len && uart_putc(UART_TUYA, tx_buf[tx_idx]))
    tx_idx++;
  if (tx_len && tx_idx == tx_len && uart_tx_idle(UART_TUYA))
    tx_len = 0;

  parse();

  if (pending_status == TUYA_REQ_WAITING && timer_expired(&pending_timer))
  {
    pending_status = TUYA_REQ_IDLE;
    pending_cmd = 0;
  }

  if (timer_expired(&heartbeat_timer))
    tuya_module_reset();
}
