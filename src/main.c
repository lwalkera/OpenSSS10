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

  /* Keep the watchdog stopped while the core is halted by a debugger */
  RCC->APB2ENR |= RCC_APB2ENR_DBGMCUEN;
  DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

  IWDG->KR = 0xCCCC;
  IWDG->KR = 0x5555;
  IWDG->PR = 6;
  IWDG->RLR = 0x618;
  while (IWDG->SR)
  {
  }
  IWDG->KR = 0xAAAA;
}

static inline void iwdg_refresh(void)
{
  IWDG->KR = 0xAAAA;
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
