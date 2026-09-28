/*
 * PIR smart switch firmware for the FT32F030C8AT7, reimplementing the stock
 * application described in firmware_spec.md.
 */
#include "adc.h"
#include "app.h"
#include "board.h"
#include "led.h"
#include "light.h"
#include "localtime.h"
#include "ota.h"
#include "pir.h"
#include "sound.h"
#include "timebase.h"
#include "triac.h"
#include "tuya.h"
#include "uart.h"

/* IWDG: LSI 40 kHz / 256, reload 0x618 -> ~10 s */
static void iwdg_start(void)
{
  RCC->CSR |= RCC_CSR_LSION;
  while (!(RCC->CSR & RCC_CSR_LSIRDY))
  {
  }

  /*
   * No DBGMCU_APB1_FZ.DBG_IWDG_STOP: on the FT32 it can only be written while
   * halted, and the RM says it enables a hardware watchdog rather than
   * freezing it (see openocd/ft32f0x.cfg).
   */

  /* The FT32 needs >= 3 LSI cycles (~75 us) between IWDG_KR writes */
  IWDG->KR = 0xCCCC;
  delay_us(100);
  IWDG->KR = 0x5555;
  IWDG->PR = 6;
  IWDG->RLR = 0x618;
  while (IWDG->SR)
  {
  }
  delay_us(100);
  IWDG->KR = 0xAAAA;
}

/*
 * Refresh at most every 2 ms: the FT32 needs >= 3 LSI cycles (~75 us)
 * between IWDG_KR writes, and a main-loop pass can be much shorter.
 * 2 ms because a 1 ms timer can expire just after it was set.
 */
static void iwdg_refresh(void)
{
  static sw_timer_t next;

  if (timer_expired(&next))
  {
    timer_set_ms(&next, 2);
    IWDG->KR = 0xAAAA;
  }
}

int main(void)
{
  /* Startup has already relocated the vector table and remapped SRAM to 0 */
  timebase_init();
  uart_init();
  led_init();
  adc_init();
  light_init();
  sound_init();
  adc_set_callbacks(sound_cb, light_cb);
  pir_init();
  triac_init();
  wallswitch_init();
  tuya_init();
  localtime_init();
  app_init();
  ota_init();
  app_register_tuya();

  __enable_irq();

  app_load();
  iwdg_start();

  for (;;)
  {
    iwdg_refresh();
    clock_update();
    uart_poll();
    led_poll();
    button_poll();
    adc_sampler_poll();
    pir_poll();
    triac_poll();
    wallswitch_poll();
    tuya_poll();
    time_poll();
    app_poll();
    ota_poll();
  }
}
