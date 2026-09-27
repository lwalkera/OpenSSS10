#include "uart.h"

#include "board.h"
#include "timebase.h"

#define RX_SIZE  512
#define TX_SIZE  4

/* DMA CCR: enable, circular, memory increment, priority high, periph->mem, 8-bit */
#define DMA_CCR_RX  0x20A1u

typedef struct {
  USART_TypeDef *usart;
  DMA_Channel_TypeDef *rx_dma;
  sw_timer_t rx_idle;
  uint8_t *rx_buf;
  uint8_t tx_ring[TX_SIZE];
  uint16_t rx_rd;
  uint16_t rx_wr;
  uint8_t tx_rd, tx_wr;
  bool enabled;
} uart_port_t;

static uint8_t rx_buf_tuya[RX_SIZE];
#ifdef FEATURE_TELEMETRY
static uint8_t rx_buf_debug[RX_SIZE];
#endif

static uart_port_t ports[UART_COUNT];

static void port_init(uart_port_t *p, USART_TypeDef *usart, DMA_Channel_TypeDef *dma,
                      uint8_t *buf, uint16_t brr)
{
  p->usart = usart;
  p->rx_dma = dma;
  p->rx_buf = buf;
  p->enabled = true;

  usart->CR1 = 0;
  usart->BRR = brr;
  usart->CR3 = USART_CR3_DMAR;

  dma->CCR = 0;
  dma->CPAR = (uint32_t)&usart->RDR;
  dma->CMAR = (uint32_t)buf;
  dma->CNDTR = RX_SIZE;
  dma->CCR = DMA_CCR_RX;

  usart->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
}

void uart_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_DMA1EN | RCC_AHBENR_GPIOAEN;

  /* USART1: PA9 TX, PA10 RX (AF1) -> Tuya module, DMA1 CH3 */
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  gpio_af(GPIOA, 9, 1);
  gpio_af(GPIOA, 10, 1);
  port_init(&ports[UART_TUYA], USART1, DMA1_Channel3, rx_buf_tuya, 0x1388);

#ifdef FEATURE_TELEMETRY
  /* USART2: PA2 TX, PA3 RX (AF1) -> debug/telemetry, DMA1 CH5 */
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
  gpio_af(GPIOA, 2, 1);
  gpio_af(GPIOA, 3, 1);
  port_init(&ports[UART_DEBUG], USART2, DMA1_Channel5, rx_buf_debug, 0x01A1);
#endif
}

void uart_poll(void)
{
  for (int i = 0; i < UART_COUNT; i++)
  {
    uart_port_t *p = &ports[i];

    if (!p->enabled)
      continue;

    uint16_t wr = (uint16_t)(RX_SIZE - p->rx_dma->CNDTR);
    if (wr == RX_SIZE)
      wr = 0;
    if (wr != p->rx_wr)
    {
      p->rx_wr = wr;
      timer_set_ms(&p->rx_idle, 5);
    }

    if (p->tx_rd != p->tx_wr && (p->usart->ISR & USART_ISR_TXE))
    {
      p->usart->TDR = p->tx_ring[p->tx_rd];
      p->tx_rd = (uint8_t)((p->tx_rd + 1) % TX_SIZE);
    }
  }
}

uint16_t uart_avail(int port)
{
  const uart_port_t *p = &ports[port];

  return (uint16_t)((p->rx_wr + RX_SIZE - p->rx_rd) % RX_SIZE);
}

int uart_getc(int port)
{
  uart_port_t *p = &ports[port];

  if (p->rx_rd == p->rx_wr)
    return -1;

  uint8_t c = p->rx_buf[p->rx_rd];
  p->rx_rd = (uint16_t)((p->rx_rd + 1) % RX_SIZE);
  return c;
}

uint16_t uart_read(int port, uint8_t *buf, uint16_t n)
{
  uint16_t got = 0;
  int c;

  while (got < n && (c = uart_getc(port)) >= 0)
    buf[got++] = (uint8_t)c;
  return got;
}

void uart_flush(int port)
{
  ports[port].rx_rd = ports[port].rx_wr;
}

bool uart_putc(int port, uint8_t c)
{
  uart_port_t *p = &ports[port];
  uint8_t next = (uint8_t)((p->tx_wr + 1) % TX_SIZE);

  if (!p->enabled || next == p->tx_rd)
    return false;

  p->tx_ring[p->tx_wr] = c;
  p->tx_wr = next;
  return true;
}

bool uart_tx_idle(int port)
{
  return ports[port].tx_rd == ports[port].tx_wr;
}

bool uart_rx_idle(int port)
{
  return timer_expired(&ports[port].rx_idle);
}

void dbg_putc(char c)
{
#ifdef FEATURE_TELEMETRY
  while (!(USART2->ISR & USART_ISR_TXE))
  {
  }
  USART2->TDR = (uint8_t)c;
#else
  (void)c;
#endif
}

void dbg_puts(const char *s)
{
  while (*s)
    dbg_putc(*s++);
}

void dbg_putd(int32_t v)
{
  char buf[12];
  int n = 0;
  uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;

  do
  {
    buf[n++] = (char)('0' + u % 10);
    u /= 10;
  } while (u);
  if (v < 0)
    dbg_putc('-');
  while (n)
    dbg_putc(buf[--n]);
}
