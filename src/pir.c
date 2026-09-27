#include "pir.h"

#include "board.h"
#include "pir_detect.h"
#include "timebase.h"
#include "uart.h"

#define WARMUP_SAMPLES    500    /* ~7.9 s at 63.5 Hz */
#define SAMPLE_TIMEOUT_MS 1000

enum { ST_RESET, ST_WARMUP, ST_RUN };

static uint8_t state;
static bool ready;
static uint32_t samples;
static sw_timer_t sample_timeout;
static pir_detect_t det;

/*
 * One bit slot, ~13.25 us (measured on the stock firmware): drive low ~1.1 us,
 * drive high ~2.6 us, release, sample ~9 us later. Interrupts are disabled per
 * slot only, so TRIAC gate events are delayed by at most one slot.
 */
static unsigned read_slot(void)
{
  unsigned bit;

  __disable_irq();
  gpio_clear(PIR_PORT, PIR_PIN);
  gpio_mode(PIR_PORT, PIR_PIN, GPIO_MODE_OUT);
  delay_cycles(40);
  gpio_set(PIR_PORT, PIR_PIN);
  delay_cycles(110);
  gpio_mode(PIR_PORT, PIR_PIN, GPIO_MODE_IN);
  delay_cycles(420);
  bit = gpio_read(PIR_PORT, PIR_PIN);
  __enable_irq();
  return bit;
}

/* Read one frame; an invalid frame reads as 0, as in the stock firmware */
static int16_t read_sample(void)
{
  delay_us(40);
  unsigned hdr1 = read_slot();
  unsigned hdr0 = read_slot();
  uint16_t v = 0;
  for (int i = 0; i < 16; i++)
    v = (uint16_t)((v << 1) | read_slot());
  unsigned trailer = read_slot();

  if (hdr1 != 1 || hdr0 != 0 || trailer != 0)
    return 0;
  return (int16_t)v;
}

void pir_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
  gpio_mode(PIR_PORT, PIR_PIN, GPIO_MODE_IN);
  gpio_pull(PIR_PORT, PIR_PIN, GPIO_PULL_NONE);
  gpio_speed_high(PIR_PORT, PIR_PIN);
  state = ST_RESET;
  timer_set_ms(&sample_timeout, SAMPLE_TIMEOUT_MS);
}

void pir_set_sensitivity(uint8_t sensitivity)
{
  pir_detect_set_sensitivity(&det, sensitivity);
}

bool pir_motion(void)
{
  return det.motion;
}

bool pir_ready(void)
{
  return ready;
}

void pir_poll(void)
{
  /* The HS6605 holds the line high while a sample is ready */
  if (!gpio_read(PIR_PORT, PIR_PIN))
  {
    if (timer_expired(&sample_timeout))
    {
      ready = false;
      det.motion = false;
      state = ST_RESET;
    }
    return;
  }

  int16_t v = read_sample();
  samples++;
  timer_set_ms(&sample_timeout, SAMPLE_TIMEOUT_MS);

  switch (state)
  {
  case ST_RESET:
    samples = 0;
    state = ST_WARMUP;
    break;

  case ST_WARMUP:
    /* Discard the power-up transient */
    if (samples >= WARMUP_SAMPLES)
    {
      pir_detect_reset(&det);
      ready = true;
      det.motion = false;
      state = ST_RUN;
    }
    break;

  default:
    pir_detect_sample(&det, v);
    break;
  }

#ifdef FEATURE_TELEMETRY
  dbg_putd(v);
  dbg_putc(',');
  dbg_putd(det.motion ? 100 : 0);
  dbg_puts("\r\n");
#endif
}
