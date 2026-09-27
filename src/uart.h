/* USART driver: circular DMA receive, polled transmit ring (spec §5.6, §7) */
#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>

enum {
  UART_TUYA = 0,   /* USART1, 9600 8N1 */
  UART_DEBUG = 1,  /* USART2, 115200 8N1 */
  UART_COUNT
};

void uart_init(void);

/* Track DMA write position and pump one TX byte per port; call every loop */
void uart_poll(void);

uint16_t uart_avail(int port);
int uart_getc(int port);                       /* -1 if empty */
uint16_t uart_read(int port, uint8_t *buf, uint16_t n);
void uart_flush(int port);
bool uart_putc(int port, uint8_t c);           /* false if the TX ring is full */
bool uart_tx_idle(int port);

/* True once no byte has arrived for 5 ms */
bool uart_rx_idle(int port);

/* Blocking debug output on USART2, bypassing the ring */
void dbg_putc(char c);
void dbg_puts(const char *s);
void dbg_putd(int32_t v);

#endif
