#include "factory.h"

#ifdef FEATURE_FACTORY_TEST

#include <stdbool.h>

#include "app.h"
#include "dp.h"
#include "led.h"
#include "light.h"
#include "pir.h"
#include "sound.h"
#include "timebase.h"
#include "triac.h"
#include "tuya.h"

#define CHASE_MS          400
#define TEST_TIME_S       60
#define WIFI_RETRY_S      5
#define WIFI_MIN_SIGNAL   60
#define LIGHT_MIN_SPAN    80
#define SOUND_THRESHOLD   90

/* Test bits: each LED stays lit until its test passes */
#define PASS_WIFI   0x1u   /* white */
#define PASS_PIR    0x2u   /* red */
#define PASS_SOUND  0x4u   /* PB13 */
#define PASS_LIGHT  0x8u   /* PB14 */
#define PASS_ALL    0xFu

enum { FT_CHASE, FT_TESTS, FT_LOAD, FT_HALT };

static uint8_t state;
static uint8_t chase;
static uint8_t passed;
static bool load_on;
static sw_timer_t timer;
static sw_timer_t wifi_timer;

void factory_start(void)
{
  app_load_defaults();
  app_save_all();
  sound_set_threshold(SOUND_THRESHOLD);

  chase = 0;
  timer_set_ms(&timer, 0);
  state = FT_CHASE;
}

void factory_wifi_result(const uint8_t *data, uint16_t len)
{
  if (state == FT_TESTS && len >= 2 && data[1] >= WIFI_MIN_SIGNAL)
    passed |= PASS_WIFI;
}

void factory_poll(void)
{
  switch (state)
  {
  case FT_CHASE:
    /* One output at a time until the button is released */
    if (timer_expired(&timer))
    {
      timer_set_ms(&timer, CHASE_MS);
      app_set_led_word((uint16_t)(LW_FACTORY | (1u << chase)));
      chase = (uint8_t)((chase + 1) & 3);
    }
    if (!button_held())
    {
      passed = 0;
      light_reset_span();
      (void)button_take_press();
      timer_set_s(&timer, TEST_TIME_S);
      timer_set_s(&wifi_timer, 0);
      state = FT_TESTS;
    }
    break;

  case FT_TESTS:
    if (!(passed & PASS_WIFI) && timer_expired(&wifi_timer) && tuya_link_idle())
    {
      tuya_request(TUYA_CMD_WIFI_TEST, 0, 0);
      timer_set_s(&wifi_timer, WIFI_RETRY_S);
    }
    if (pir_motion())
      passed |= PASS_PIR;
    if (sound_triggered())
      passed |= PASS_SOUND;
    if (light_span() > LIGHT_MIN_SPAN)
      passed |= PASS_LIGHT;

    app_set_led_word((uint16_t)(LW_FACTORY | (~passed & PASS_ALL)));

    if (button_take_press() && passed == PASS_ALL)
    {
      load_on = false;
      state = FT_LOAD;
    }
    else if (timer_expired(&timer))
    {
      state = FT_HALT;   /* LEDs keep showing the failed tests */
    }
    break;

  case FT_LOAD:
    /* Each press toggles the TRIAC and the white LEDs at 100 % */
    if (button_take_press())
    {
      load_on = !load_on;
      dp.sw = load_on;
      triac_set(load_on, 0);
    }
    app_set_led_word((uint16_t)(LW_FACTORY | (load_on ? LED_WHITE : 0)));
    break;

  default:
    break;
  }
}

#else

void factory_start(void)
{
}

void factory_poll(void)
{
}

void factory_wifi_result(const uint8_t *data, uint16_t len)
{
  (void)data;
  (void)len;
}

#endif
