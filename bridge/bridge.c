/*
 * Wi-Fi UART bridge for flashing the Beken module (e.g. with OpenBeken)
 * without soldering.
 *
 * Loaded into SRAM over SWD by `make bridge-run`; flash is not touched, so
 * a power cycle brings back the installed firmware.
 *
 *   diag UART  USART2  PA2 TX / PA3 RX   <->  host USB-serial adapter
 *   Wi-Fi UART USART1  PA9 TX / PA10 RX  <->  Beken module
 *
 * Both run at 115200 8N1, the Beken boot ROM's rate. Bytes are forwarded
 * unchanged in both directions.
 *
 * Pressing the button (PF7) pulses the module reset line (PB10) low for
 * 100 ms so the Beken restarts into its boot ROM: start the flasher first,
 * then press the button. The module is also reset once at start-up.
 *
 * LEDs: green = bridge running; red = module held in reset;
 * white flickers with traffic. The TRIAC gate (PB3) is held off throughout.
 */
#include <stdbool.h>
#include <stdint.h>

#include "ft32f0xx.h"

#define BAUD_BRR        417u     /* 48 MHz / 115200 */
#define RESET_PULSE_MS  100u
#define DEBOUNCE_MS     30u
#define ACTIVITY_MS     20u
#define QUEUE_SIZE      512u     /* power of two */

typedef struct {
  uint8_t buf[QUEUE_SIZE];
  uint16_t head, tail;
} queue_t;

static queue_t to_module, to_host;
static volatile uint32_t ms_ticks;

static void q_put(queue_t *q, uint8_t c)
{
  uint16_t next = (uint16_t)((q->head + 1) & (QUEUE_SIZE - 1));
  if (next != q->tail)   /* drop if full; cannot happen at equal baud rates */
  {
    q->buf[q->head] = c;
    q->head = next;
  }
}

static int q_get(queue_t *q)
{
  if (q->head == q->tail)
    return -1;
  uint8_t c = q->buf[q->tail];
  q->tail = (uint16_t)((q->tail + 1) & (QUEUE_SIZE - 1));
  return c;
}

static void gpio_mode(GPIO_TypeDef *port, unsigned pin, unsigned mode)
{
  port->MODER = (port->MODER & ~(3u << (pin * 2))) | (mode << (pin * 2));
}

static void gpio_af1(GPIO_TypeDef *port, unsigned pin)
{
  unsigned reg = pin >> 3, shift = (pin & 7) * 4;
  port->AFR[reg] = (port->AFR[reg] & ~(0xFu << shift)) | (1u << shift);
  port->OSPEEDR |= 3u << (pin * 2);
  gpio_mode(port, pin, 2);
}

/* HSI 8 MHz / PREDIV 1 x 6 = 48 MHz (spec §2.2) */
static void clock_init(void)
{
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY))
  {
  }
  RCC->CFGR &= ~RCC_CFGR_SW;                     /* run from HSI while changing the PLL */
  while (RCC->CFGR & RCC_CFGR_SWS)
  {
  }
  RCC->CR &= ~RCC_CR_PLLON;
  while (RCC->CR & RCC_CR_PLLRDY)
  {
  }
  FLASH->ACR = FLASH_ACR_PRFTBE | 1u;            /* one wait state */
  RCC->CFGR2 &= ~0xFu;                           /* PREDIV /1 */
  RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL | RCC_CFGR_HPRE | RCC_CFGR_PPRE)) |
              RCC_CFGR_PLLSRC_HSI_PREDIV | RCC_CFGR_PLLMULL6;
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY))
  {
  }
  RCC->CFGR |= RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
  {
  }
}

static void usart_init(USART_TypeDef *u)
{
  u->CR1 = 0;
  u->BRR = BAUD_BRR;
  u->CR2 = 0;
  u->CR3 = 0;
  u->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
}

/* Move one received byte (if any) into q; clear errors so RX keeps running */
static bool usart_rx(USART_TypeDef *u, queue_t *q)
{
  uint32_t isr = u->ISR;

  if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
    u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_PECF;
  if (!(isr & USART_ISR_RXNE))
    return false;
  q_put(q, (uint8_t)u->RDR);
  return true;
}

static bool usart_tx(USART_TypeDef *u, queue_t *q)
{
  if (!(u->ISR & USART_ISR_TXE))
    return false;
  int c = q_get(q);
  if (c < 0)
    return false;
  u->TDR = (uint8_t)c;
  return true;
}

/* Millisecond tick from SysTick's COUNTFLAG, no interrupt */
static void tick_poll(void)
{
  if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)
    ms_ticks++;
}

static void led(unsigned pin, bool on)
{
  GPIOB->BSRR = on ? (1u << pin) : (1u << (pin + 16));
}

static void white(bool on)
{
  /* White LEDs are active low: PA7, PB0, PB1 */
  GPIOA->BSRR = on ? (1u << (7 + 16)) : (1u << 7);
  GPIOB->BSRR = on ? (3u << 16) : 3u;
}

/* Called from the naked entry stub, so it must survive as a real function */
__attribute__((used, noinline))
static void bridge_main(void)
{
  extern uint32_t _sbss, _ebss;

  for (uint32_t *p = &_sbss; p < &_ebss; p++)
    *p = 0;

  clock_init();

  SysTick->LOAD = 48000u - 1u;
  SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

  RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOFEN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

  /* TRIAC gate off (active low) before anything else */
  GPIOB->BSRR = 1u << 3;
  gpio_mode(GPIOB, 3, 1);

  /* Module reset (PB10), active low: assert now, released after 100 ms */
  GPIOB->BRR = 1u << 10;
  gpio_mode(GPIOB, 10, 1);

  /* LEDs as plain outputs: PB13 green, PB15 red (active high); white off */
  GPIOB->BRR = (1u << 13) | (1u << 14) | (1u << 15);
  gpio_mode(GPIOB, 13, 1);
  gpio_mode(GPIOB, 14, 1);
  gpio_mode(GPIOB, 15, 1);
  white(false);
  gpio_mode(GPIOA, 7, 1);
  gpio_mode(GPIOB, 0, 1);
  gpio_mode(GPIOB, 1, 1);

  /* Button PF7, pull-up, active low */
  gpio_mode(GPIOF, 7, 0);
  GPIOF->PUPDR = (GPIOF->PUPDR & ~(3u << 14)) | (1u << 14);

  gpio_af1(GPIOA, 9);
  gpio_af1(GPIOA, 10);
  gpio_af1(GPIOA, 2);
  gpio_af1(GPIOA, 3);
  usart_init(USART1);
  usart_init(USART2);

  led(13, true);
  led(15, true);

  uint32_t reset_until = RESET_PULSE_MS;   /* start-up reset pulse */
  bool in_reset = true;
  bool button_down = false;
  uint32_t button_since = 0;
  uint32_t activity_until = 0;

  for (;;)
  {
    tick_poll();
    IWDG->KR = 0xAAAA;   /* harmless if the watchdog is not running */

    bool moved = false;
    moved |= usart_rx(USART2, &to_module);
    moved |= usart_rx(USART1, &to_host);
    moved |= usart_tx(USART1, &to_module);
    moved |= usart_tx(USART2, &to_host);

    if (moved)
      activity_until = ms_ticks + ACTIVITY_MS;
    white((int32_t)(activity_until - ms_ticks) > 0);

    /* Button: debounced press starts a reset pulse */
    bool pressed = !(GPIOF->IDR & (1u << 7));
    if (pressed != button_down)
    {
      if (ms_ticks - button_since >= DEBOUNCE_MS)
      {
        button_down = pressed;
        if (pressed && !in_reset)
        {
          GPIOB->BRR = 1u << 10;
          led(15, true);
          in_reset = true;
          reset_until = ms_ticks + RESET_PULSE_MS;
        }
      }
    }
    else
    {
      button_since = ms_ticks;
    }

    if (in_reset && (int32_t)(ms_ticks - reset_until) >= 0)
    {
      GPIOB->BSRR = 1u << 10;
      led(15, false);
      in_reset = false;
    }
  }
}

/* Entry at 0x20000000: set up the stack, then run. Interrupts stay off. */
__attribute__((naked, section(".entry"), used))
void bridge_entry(void)
{
  __asm volatile(
    "cpsid i\n"
    "ldr r0, =_estack\n"
    "mov sp, r0\n"
    "bl bridge_main\n"
    "b .\n");
}
