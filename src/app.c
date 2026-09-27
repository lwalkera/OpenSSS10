#include "app.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "dp.h"
#include "factory.h"
#include "led.h"
#include "light.h"
#include "localtime.h"
#include "ota.h"
#include "pir.h"
#include "schedule.h"
#include "sound.h"
#include "storage.h"
#include "timebase.h"
#include "triac.h"
#include "tuya.h"

#define SETTINGS_WORDS        14
#define SAVE_DEBOUNCE_MS      3000
#define PREVIEW_MS            5000
#define TEST_MODE_TIMEOUT_S   900
#define TEST_HOLDOFF_S        3
#define BUTTON_DEBOUNCE_MS    50
#define BUTTON_LONG_MS        6000
#define DEFAULT_TAG           "CF.20"

/* Load states (spec §14.2) */
enum {
  LS_BOOT = 0,
  LS_OFF_REQ = 3,
  LS_HOLDOFF = 4,
  LS_ARMED = 5,
  LS_ON_REQ = 6,
  LS_ON = 7,
  LS_FACTORY = 8,
};

/* Pairing / connection supervisor states (spec §14.3) */
enum {
  SUP_WAIT_MODULE = 0x00,
  SUP_CHECK = 0x16,
  SUP_LOST = 0x17,
  SUP_LOST_LONG = 0x18,
  SUP_PAIR_ENTER = 0x19,
  SUP_PAIR_DELAY = 0x1A,
  SUP_PAIR_SEND = 0x1B,
  SUP_PAIR_WAIT = 0x1C,
  SUP_PAIR_CONNECT = 0x1D,
  SUP_PAIR_DONE = 0x1E,
};

/* "connected" values */
enum { CONN_PAIRING = 0, CONN_CLOUD = 1, CONN_TIMEOUT = 2 };

/* DP 115 values */
enum {
  TEST_NONE = 0,
  TEST_PIR_RESULT = 1,
  TEST_PIR = 2,
  TEST_SOUND_RESULT = 3,
  TEST_SOUND = 4,
  TEST_LIGHT = 5,
};

static struct {
  bool boot_done;
  uint8_t last_src;
  uint8_t load_state;
  sw_timer_t load_timer;         /* hold-off or auto-off */

  uint8_t connected;
  uint8_t wifi_status;
  uint8_t sup_state;
  uint8_t pair_retries;
  sw_timer_t sup_timer;
  sw_timer_t pair_timer;         /* 180 s overall pairing timer */

  uint16_t led_word;
  sw_timer_t preview_timer;

  bool settings_dirty;
  bool sched_dirty;
  sw_timer_t save_timer;

  uint32_t last_sec;
  sw_timer_t test_timer;

  /* Last rendered LED inputs */
  uint16_t r_word;
  bool r_ambient;
  uint8_t r_light;
  bool r_valid;
} app;

/* ------------------------------------------------------------------------ */
/* Settings persistence (spec §13.1)                                        */

static void mark_settings_dirty(void)
{
  app.settings_dirty = true;
  timer_set_ms(&app.save_timer, SAVE_DEBOUNCE_MS);
}

static void mark_sched_dirty(void)
{
  app.sched_dirty = true;
  timer_set_ms(&app.save_timer, SAVE_DEBOUNCE_MS);
}

static void save_settings(void)
{
  uint32_t w[SETTINGS_WORDS] = {
    dp.settings_w0, dp.paired, dp.holdoff_s, dp.fade_in, dp.fade_out,
    dp.lvl_white, dp.lvl_cyan, dp.lvl_status, (uint32_t)dp.dp107,
    (uint32_t)dp.dp123, (uint32_t)dp.dp124, dp.pir_sens, dp.sound_thr, dp.ambient,
  };

  storage_write(SLOT_SETTINGS, w, SETTINGS_WORDS);
}

static bool load_settings(void)
{
  uint32_t w[SETTINGS_WORDS];
  uint32_t n;

  if (!storage_read(SLOT_SETTINGS, w, SETTINGS_WORDS, &n) || n != SETTINGS_WORDS)
    return false;

  dp.settings_w0 = w[0];
  dp.paired = w[1] != 0;
  dp.holdoff_s = w[2];
  dp.fade_in = (uint8_t)(w[3] > 3 ? 3 : w[3]);
  dp.fade_out = (uint8_t)(w[4] > 3 ? 3 : w[4]);
  dp.lvl_white = (uint8_t)(w[5] > 100 ? 100 : w[5]);
  dp.lvl_cyan = (uint8_t)(w[6] > 100 ? 100 : w[6]);
  dp.lvl_status = (uint8_t)(w[7] > 100 ? 100 : w[7]);
  dp.dp107 = (int32_t)w[8];
  dp.dp123 = (int32_t)w[9];
  dp.dp124 = (int32_t)w[10];
  dp.pir_sens = (uint8_t)(w[11] > 2 ? 2 : w[11]);
  dp.sound_thr = (uint8_t)(w[12] < 20 ? 20 : w[12] > 100 ? 100 : w[12]);
  dp.ambient = w[13] != 0;
  return true;
}

static void load_tag(void)
{
  uint32_t w[TAG_MAX];
  uint32_t n;

  if (!storage_read(SLOT_TAG, w, TAG_MAX, &n))
  {
    n = sizeof(DEFAULT_TAG) - 1;
    for (uint32_t i = 0; i < n; i++)
      w[i] = (uint8_t)DEFAULT_TAG[i];
    storage_write(SLOT_TAG, w, n);
  }
  for (uint32_t i = 0; i < n; i++)
    dp.tag[i] = (char)w[i];
  dp.tag[n] = '\0';
}

/* Push setting values into the modules that use them */
static void apply_settings(void)
{
  pir_set_sensitivity(dp.pir_sens);
  sound_set_threshold(dp.sound_thr);
}

void app_save_all(void)
{
  save_settings();
  schedule_save();
  app.settings_dirty = false;
  app.sched_dirty = false;
}

void app_load_defaults(void)
{
  dp_load_defaults();
  schedule_load_defaults();
  apply_settings();
}

static void save_poll(void)
{
  if (!(app.settings_dirty || app.sched_dirty) || !timer_expired(&app.save_timer))
    return;
  /* Don't stall the CPU on flash while Tuya traffic or a fade is running */
  if ((tuya_module_alive() && !tuya_link_idle()) || !triac_done())
    return;

  if (app.settings_dirty)
    save_settings();
  if (app.sched_dirty)
    schedule_save();
  app.settings_dirty = false;
  app.sched_dirty = false;
}

void app_load(void)
{
  if (!load_settings())
    dp_load_defaults();
  if (!schedule_load())
    schedule_load_defaults();
  load_tag();
  apply_settings();
}

/* ------------------------------------------------------------------------ */
/* Switching and the load state machine (spec §14.1, §14.2)                 */

void app_switch(bool on, uint8_t src)
{
  dp.sw = on;
  app.load_state = on ? LS_ON_REQ : LS_OFF_REQ;
  app.last_src = src;
  dp_report(RB_SWITCH);
}

void app_set_led_word(uint16_t word)
{
  app.led_word = word;
}

static void set_color(uint16_t color)
{
  app.led_word = (uint16_t)((app.led_word & ~LW_COLOR_MASK) | color);
}

/* Indicator colour for the load state (spec §10.6) */
static void update_color(void)
{
  const sched_entry_t *rec = schedule_active();

  switch (app.load_state)
  {
  case LS_HOLDOFF:
    set_color(LW_CYAN);
    break;
  case LS_ARMED:
    set_color(rec && rec->mode == SCHED_MODE_PIR && light_dark() ? LW_WHITE : LW_CYAN);
    break;
  case LS_ON:
    set_color(rec && rec->off_mode == SCHED_OFF_AUTO ? LW_WHITE : LW_CYAN);
    break;
  default:
    break;
  }
}

#ifdef FEATURE_SENSOR_TEST
static void test_result(uint8_t result)
{
  dp.test_mode = result;
  dp_report(RB_TEST_MODE);
  app.led_word &= (uint16_t)~LW_SENSOR_TEST;
  app_switch(true, SRC_SENSOR);
}
#endif

static void load_poll(void)
{
  const sched_entry_t *rec = schedule_active();

  switch (app.load_state)
  {
  case LS_BOOT:
#ifdef FEATURE_FACTORY_TEST
    if (button_held())
    {
      app.boot_done = true;
      app.load_state = LS_FACTORY;
      factory_start();
      break;
    }
#endif
    app.boot_done = true;
    if (!dp.paired)
      app.sup_state = SUP_PAIR_ENTER;
    app.load_state = LS_ARMED;
    break;

  case LS_OFF_REQ:
    triac_set(false, dp.fade_out);
    app.load_state = LS_ARMED;
    if (rec && rec->mode == SCHED_MODE_PIR)
    {
      if (app.last_src == SRC_MANUAL)
      {
        timer_set_s(&app.load_timer, dp.holdoff_s);
        app.load_state = LS_HOLDOFF;
      }
      else if (app.last_src == SRC_TEST)
      {
        timer_set_s(&app.load_timer, TEST_HOLDOFF_S);
        app.load_state = LS_HOLDOFF;
      }
    }
    break;

  case LS_HOLDOFF:
    if (timer_expired(&app.load_timer))
      app.load_state = LS_ARMED;
    break;

  case LS_ARMED:
#ifdef FEATURE_SENSOR_TEST
    if (dp.test_mode == TEST_PIR && pir_motion())
    {
      test_result(TEST_PIR_RESULT);
      break;
    }
    if (dp.test_mode == TEST_SOUND && sound_triggered())
    {
      test_result(TEST_SOUND_RESULT);
      break;
    }
#endif
    if (rec && rec->mode == SCHED_MODE_PIR && light_dark() && pir_motion())
      app_switch(true, SRC_SENSOR);
    break;

  case LS_ON_REQ:
    triac_set(true, dp.fade_in);
    if (rec && rec->off_mode == SCHED_OFF_AUTO)
      timer_set_s(&app.load_timer, rec->autooff_s);
    app.load_state = LS_ON;
    break;

  case LS_ON:
    if (rec && rec->off_mode == SCHED_OFF_AUTO)
    {
      if (pir_motion() || sound_triggered())
        timer_set_s(&app.load_timer, rec->autooff_s);
      else if (timer_expired(&app.load_timer))
        app_switch(false, SRC_SENSOR);
    }
    break;

  default:
#ifdef FEATURE_FACTORY_TEST
    factory_poll();
#endif
    return;
  }

  update_color();
}

/* ------------------------------------------------------------------------ */
/* Pairing and connection supervisor (spec §14.3)                           */

static void set_pair_led(uint16_t bits)
{
  app.led_word = (uint16_t)((app.led_word & ~LW_PAIR_MASK) | bits);
}

static void supervisor_poll(void)
{
  bool cloud = app.connected == CONN_CLOUD;

  switch (app.sup_state)
  {
  case SUP_WAIT_MODULE:
    if (tuya_module_alive())
      app.sup_state = SUP_CHECK;
    break;

  case SUP_CHECK:
    if (app.wifi_status != 4)
    {
      timer_set_s(&app.sup_timer, 20);
      app.sup_state = SUP_LOST;
    }
    break;

  case SUP_LOST:
    if (app.wifi_status == 4)
      app.sup_state = SUP_CHECK;
    else if (timer_expired(&app.sup_timer))
    {
      if (dp.paired)
        set_pair_led(LW_PAIR_LOST);
      timer_set_s(&app.sup_timer, 600);
      app.sup_state = SUP_LOST_LONG;
    }
    break;

  case SUP_LOST_LONG:
    if (app.wifi_status == 4)
      app.sup_state = SUP_CHECK;
    else if (timer_expired(&app.sup_timer))
    {
      tuya_module_reset();
      app.sup_state = SUP_WAIT_MODULE;
    }
    break;

  case SUP_PAIR_ENTER:
    timer_set_s(&app.sup_timer, 5);
    app.pair_retries = 0;
    set_pair_led(LW_PAIR_BLINK);
    app.connected = CONN_PAIRING;
    app.sup_state = SUP_PAIR_DELAY;
    break;

  case SUP_PAIR_DELAY:
    if (timer_expired(&app.sup_timer))
    {
      /* The overall timer runs from the first attempt, not each retry */
      if (app.pair_retries == 0)
        timer_set_s(&app.pair_timer, 180);
      app.sup_state = SUP_PAIR_SEND;
    }
    break;

  case SUP_PAIR_SEND:
    if (cloud)
      app.sup_state = SUP_CHECK;
    else if (timer_expired(&app.pair_timer))
      app.sup_state = SUP_PAIR_CONNECT;
    else if (tuya_link_idle())
    {
      static const uint8_t smartconfig = 0x00;
      if (tuya_request(TUYA_CMD_PAIR_MODE, &smartconfig, 1))
      {
        app.pair_retries++;
        app.sup_state = SUP_PAIR_WAIT;
      }
    }
    break;

  case SUP_PAIR_WAIT:
    if (cloud)
      app.sup_state = SUP_CHECK;
    else if (tuya_request_status() == TUYA_REQ_RECEIVED || app.pair_retries >= 3)
      app.sup_state = SUP_PAIR_CONNECT;
    else if (tuya_request_status() == TUYA_REQ_IDLE)
    {
      /* No response within 10 s: try again */
      timer_set_s(&app.sup_timer, 0);
      app.sup_state = SUP_PAIR_DELAY;
    }
    break;

  case SUP_PAIR_CONNECT:
    if (app.wifi_status == 4)
    {
      app_load_defaults();
      dp.paired = true;
      app_save_all();
      dp_report_all();
      app.sup_state = SUP_PAIR_DONE;
    }
    else if (timer_expired(&app.pair_timer))
    {
      app.connected = CONN_TIMEOUT;
      app.sup_state = SUP_PAIR_DONE;
    }
    break;

  case SUP_PAIR_DONE:
    set_pair_led(0);
    app.sup_state = SUP_WAIT_MODULE;
    break;

  default:
    app.sup_state = SUP_WAIT_MODULE;
    break;
  }
}

/* ------------------------------------------------------------------------ */
/* LED rendering (spec §10.4, §10.5)                                        */

static uint8_t level_by_index(unsigned idx)
{
  switch (idx & 3)
  {
  case 0:  return dp.lvl_white;
  case 1:  return dp.lvl_cyan;
  case 2:  return dp.lvl_status;
  default: return (uint8_t)(dp.dp107 < 0 ? 0 : dp.dp107 > 100 ? 100 : dp.dp107);
  }
}

static uint8_t ambient_level(uint8_t mask)
{
  uint8_t l = light_level();

  if (l < 25)
    return mask == LED_CYAN ? 15 : 20;
  if (l < 35)
    return 50;
  if (l < 50)
    return 80;
  return 100;
}

static void render_leds(void)
{
  uint16_t w = app.led_word;
  uint8_t light = light_level();
  uint8_t dlight = light > app.r_light ? light - app.r_light : app.r_light - light;

  if (app.r_valid && w == app.r_word && dp.ambient == app.r_ambient && dlight < 10)
    return;
  app.r_valid = true;
  app.r_word = w;
  app.r_ambient = dp.ambient;
  app.r_light = light;

  if (w == 0)
  {
    led_set(0, LED_OFF, 0);
  }
  else if (w & LW_PREVIEW_MASK)
  {
    led_set_target(level_by_index((w >> 12) & 3));
    led_set((uint8_t)((w >> 8) & 0xF), LED_STEADY, 0);
  }
  else if ((w & LW_PAIR_MASK) == LW_PAIR_BLINK)
  {
    led_set_target(100);
    led_set(LED_WHITE, LED_BLINK, LED_FOREVER);
  }
  else if (w & LW_PAIR_LOST)
  {
    led_set_target(dp.ambient ? ambient_level(LED_AMBER) : dp.lvl_status);
    led_set(LED_AMBER, LED_BREATHE, 0);
  }
  else if ((w & 0x30) == LW_FACTORY)
  {
    led_set_target(100);
    led_set((uint8_t)(w & 0xF), LED_STEADY, 0);
  }
  else if (w & LW_SENSOR_TEST)
  {
    led_set_target(100);
    led_set(LED_GREEN, LED_BREATHE, 0);
  }
  else
  {
    uint8_t mask = (uint8_t)(w & 0xF);
    led_set_target(dp.ambient ? ambient_level(mask) : level_by_index((w >> 4) & 3));
    led_set(mask, LED_STEADY, 0);
  }
}

static void start_preview(uint16_t bits)
{
  app.led_word = (uint16_t)((app.led_word & ~LW_PREVIEW_MASK) | bits);
  timer_set_ms(&app.preview_timer, PREVIEW_MS);
}

/* ------------------------------------------------------------------------ */
/* Once-per-second work: countdown, sensor test reports                     */

static void second_tick(void)
{
  if (dp.countdown > 0)
  {
    dp.countdown--;
    if (dp.countdown == 0)
    {
      app_switch(dp.sw_after_countdown, SRC_TIMER);
      dp_report(RB_COUNTDOWN);
    }
    else if (dp.countdown % 60 == 0)
    {
      dp_report(RB_COUNTDOWN);
    }
  }

#ifdef FEATURE_SENSOR_TEST
  if (dp.test_mode == TEST_SOUND || dp.test_mode == TEST_SOUND_RESULT)
  {
    dp.sound_peak = sound_take_peak();
    dp_report(RB_SOUND_PEAK);
  }
  if (dp.test_mode == TEST_LIGHT)
  {
    dp.light = light_level();
    dp_report(RB_LIGHT);
  }
  if (dp.test_mode != TEST_NONE && timer_expired(&app.test_timer))
  {
    dp.test_mode = TEST_NONE;
    app.led_word &= (uint16_t)~LW_SENSOR_TEST;
    dp_report(RB_TEST_MODE);
  }
#endif
}

/* ------------------------------------------------------------------------ */
/* Tuya callbacks (spec §8.3, §8.5, §12.2)                                  */

static uint8_t clamp_u8(uint32_t v, uint8_t lo, uint8_t hi)
{
  return (uint8_t)(v < lo ? lo : v > hi ? hi : v);
}

static void on_dp_record(uint8_t id, uint8_t type, const uint8_t *v, uint16_t len)
{
  uint32_t u = 0;

  (void)type;
  if (len >= 4)
    u = dp_value_be32(v);
  else if (len >= 1)
    u = v[0];

  switch (id)
  {
  case DP_SWITCH:
    app_switch(u != 0, SRC_MANUAL);
    break;
  case DP_COUNTDOWN:
    dp.countdown = u;
    dp_report(RB_COUNTDOWN);
    break;
  case DP_PIR_SENS:
    dp.pir_sens = clamp_u8(u, 0, 2);
    apply_settings();
    mark_settings_dirty();
    dp_report(RB_PIR_SENS);
    break;
  case DP_HOLDOFF:
    dp.holdoff_s = u;
    mark_settings_dirty();
    dp_report(RB_HOLDOFF);
    break;
  case DP_LVL_WHITE:
    dp.lvl_white = clamp_u8(u, 0, 100);
    start_preview(0x0100);
    mark_settings_dirty();
    dp_report(RB_LVL_WHITE);
    break;
  case DP_LVL_CYAN:
    dp.lvl_cyan = clamp_u8(u, 0, 100);
    start_preview(0x1C00);
    mark_settings_dirty();
    dp_report(RB_LVL_CYAN);
    break;
  case DP_FADE_IN:
    dp.fade_in = clamp_u8(u, 0, 3);
    mark_settings_dirty();
    dp_report(RB_FADE_IN);
    break;
  case DP_FADE_OUT:
    dp.fade_out = clamp_u8(u, 0, 3);
    mark_settings_dirty();
    dp_report(RB_FADE_OUT);
    break;
  case DP_LVL_STATUS:
    dp.lvl_status = clamp_u8(u, 0, 100);
    start_preview(0x2600);
    mark_settings_dirty();
    dp_report(RB_LVL_STATUS);
    break;
  case DP_SOUND_THR:
    dp.sound_thr = clamp_u8(u, 20, 100);
    apply_settings();
    mark_settings_dirty();
    dp_report(RB_SOUND_THR);
    break;
  case DP_107:
    dp.dp107 = (int32_t)u;
    mark_settings_dirty();
    dp_report(RB_107);
    break;
  case DP_SW_AFTER_CD:
    dp.sw_after_countdown = u != 0;
    dp_report(RB_SW_AFTER_CD);
    break;
  case DP_111:
    dp.dp111 = (int32_t)u;
    dp_report(RB_111);
    break;
  case DP_REPORT_CD:
    dp_report(RB_COUNTDOWN);
    dp_report(RB_111);
    break;
#ifdef FEATURE_SENSOR_TEST
  case DP_TEST_MODE:
    dp.test_mode = (uint8_t)u;
    timer_set_s(&app.test_timer, TEST_MODE_TIMEOUT_S);
    if (dp.test_mode == TEST_PIR || dp.test_mode == TEST_SOUND)
    {
      app_switch(false, SRC_TEST);
      app.led_word |= LW_SENSOR_TEST;
    }
    else
    {
      app.led_word &= (uint16_t)~LW_SENSOR_TEST;
    }
    dp_report(RB_TEST_MODE);
    break;
#endif
  case DP_123:
    dp.dp123 = (int32_t)u;
    mark_settings_dirty();
    dp_report(RB_123);
    break;
  case DP_124:
    dp.dp124 = (int32_t)u;
    mark_settings_dirty();
    dp_report(RB_124);
    break;
  case DP_AMBIENT:
    dp.ambient = u != 0;
    mark_settings_dirty();
    dp_report(RB_AMBIENT);
    break;
  default:
    if (id >= DP_SCHED_FIRST && id <= DP_SCHED_LAST)
    {
      unsigned slot = id - DP_SCHED_FIRST;
      if (schedule_set_from_dp(slot, v, len))
        mark_sched_dirty();
      dp_report(RB_SCHED_FIRST + slot);
    }
    break;
  }
}

static void on_dp_command(const uint8_t *data, uint16_t len)
{
  /* The factory test owns the load and LEDs */
  if (app.boot_done && app.load_state < LS_FACTORY)
    dp_parse(data, len, on_dp_record);
}

static void on_query_all(void)
{
  if (app.boot_done)
    dp_report_all();
}

static void on_wifi_status(uint8_t status)
{
  if (!app.boot_done)
    return;

  app.wifi_status = status;
  if (status == 4)
  {
    app.connected = CONN_CLOUD;
    set_pair_led(0);
    if (!dp.paired)
    {
      dp.paired = true;
      mark_settings_dirty();
    }
    dp_report_all();
  }
  else if (status == 6)
  {
    if (dp.paired)
    {
      dp.paired = false;
      mark_settings_dirty();
    }
    dp.countdown = 0;
    dp_report(RB_COUNTDOWN);
    schedule_reevaluate();
    if (app.sup_state < SUP_PAIR_ENTER)
      app.sup_state = SUP_PAIR_ENTER;
  }
}

static void on_response(uint8_t cmd, const uint8_t *data, uint16_t len)
{
  if (cmd == TUYA_CMD_LOCAL_TIME)
  {
    localtime_response(data, len);
    schedule_reevaluate();
  }
#ifdef FEATURE_FACTORY_TEST
  else if (cmd == TUYA_CMD_WIFI_TEST)
  {
    factory_wifi_result(data, len);
  }
#endif
}

void app_register_tuya(void)
{
  static const tuya_callbacks_t cb = {
    .query_all = on_query_all,
    .dp_cmd = on_dp_command,
    .wifi_status = on_wifi_status,
    .ota_start = ota_start,
    .ota_data = ota_data,
    .response = on_response,
  };

  tuya_set_callbacks(&cb);
}

/* ------------------------------------------------------------------------ */
/* Button (spec §14.5)                                                      */

static struct {
  bool stable;          /* debounced: true = pressed */
  bool raw;
  sw_timer_t debounce;
  sw_timer_t long_timer;
  bool long_fired;
  bool short_event;
  bool long_event;
} btn;

static void button_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_GPIOFEN;
  gpio_mode(BUTTON_PORT, BUTTON_PIN, GPIO_MODE_IN);
  gpio_pull(BUTTON_PORT, BUTTON_PIN, GPIO_PULL_UP);
}

void button_poll(void)
{
  bool raw = !gpio_read(BUTTON_PORT, BUTTON_PIN);

  if (raw != btn.raw)
  {
    btn.raw = raw;
    timer_set_ms(&btn.debounce, BUTTON_DEBOUNCE_MS);
    return;
  }
  if (raw != btn.stable && timer_expired(&btn.debounce))
  {
    btn.stable = raw;
    if (raw)
    {
      timer_set_ms(&btn.long_timer, BUTTON_LONG_MS);
      btn.long_fired = false;
    }
    else if (!btn.long_fired)
    {
      btn.short_event = true;
    }
  }
  if (btn.stable && !btn.long_fired && timer_expired(&btn.long_timer))
  {
    btn.long_fired = true;
    btn.long_event = true;
  }
}

bool button_held(void)
{
  return !gpio_read(BUTTON_PORT, BUTTON_PIN);
}

bool button_take_press(void)
{
  bool e = btn.short_event;

  btn.short_event = false;
  return e;
}

/* ------------------------------------------------------------------------ */
/* Wall switch (spec §14.5, [M])                                            */

#ifdef FEATURE_WALLSWITCH
/*
 * Port of the stock routine at 0x08005EEC. A live switch leg pulls PB4 low
 * through an optocoupler on each mains half-wave. After a 100 ms settling
 * window, the leg is:
 *   - dead if PB4 stays high for 100 ms;
 *   - live if, within 100 ms after the first low (plus 50 ms), PB4 changes
 *     more than 8 times with no two changes closer than a quarter of the
 *     mains period, or stays low throughout.
 * Anything in between leaves the state unchanged. Every change of the
 * debounced state toggles the load.
 */
enum { WS_INIT, WS_WAIT_MAINS, WS_ARM, WS_IDLE, WS_SETTLE, WS_COUNT };

static struct {
  uint8_t state;
  bool valid;           /* a state has been determined */
  bool live;
  bool reported_valid;  /* app has taken the first state */
  bool reported_live;
  bool level;           /* PB4 low */
  uint8_t edges;
  sw_timer_t timer;
  sw_timer_t last_edge;
} wsw;

static bool wallsw_low(void)
{
  return !gpio_read(WALLSW_PORT, WALLSW_PIN);
}

void wallswitch_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
  gpio_mode(WALLSW_PORT, WALLSW_PIN, GPIO_MODE_IN);
  gpio_pull(WALLSW_PORT, WALLSW_PIN, GPIO_PULL_UP);
  wsw.state = WS_INIT;
}

static uint32_t ms_since(const sw_timer_t *t)
{
  sw_timer_t now = clock_now();
  return (now.sec - t->sec) * 1000u + now.ms - t->ms;
}

static void wallswitch_detect(void)
{
  uint16_t period_ms = triac_period() / 100u;

  switch (wsw.state)
  {
  case WS_INIT:
    wsw.state = WS_WAIT_MAINS;
    break;

  case WS_WAIT_MAINS:
    if (period_ms)
      wsw.state = WS_ARM;
    break;

  case WS_ARM:
    timer_set_ms(&wsw.timer, 100);
    wsw.state = WS_IDLE;
    break;

  case WS_IDLE:
    if (wallsw_low())
    {
      timer_set_ms(&wsw.timer, 50);
      wsw.state = WS_SETTLE;
    }
    else if (timer_expired(&wsw.timer))
    {
      wsw.valid = true;
      wsw.live = false;
      wsw.state = WS_ARM;
    }
    break;

  case WS_SETTLE:
    if (timer_expired(&wsw.timer))
    {
      wsw.level = wallsw_low();
      wsw.edges = 0;
      timer_set_ms(&wsw.timer, 100);
      wsw.state = WS_COUNT;
    }
    break;

  default:
  {
    bool low = wallsw_low();
    if (low != wsw.level)
    {
      wsw.level = low;
      /* Edges closer than a quarter period are noise: start over */
      if (wsw.edges && ms_since(&wsw.last_edge) < period_ms / 4u)
        wsw.state = WS_ARM;
      wsw.last_edge = clock_now();
      wsw.edges++;
    }
    if (timer_expired(&wsw.timer))
    {
      if (wsw.edges > 8 || (wsw.edges == 0 && wsw.level))
      {
        wsw.valid = true;
        wsw.live = true;
      }
      wsw.state = WS_ARM;
    }
    break;
  }
  }
}

void wallswitch_poll(void)
{
  wallswitch_detect();

  if (!wsw.valid || !app.boot_done || app.load_state >= LS_FACTORY)
    return;
  if (!wsw.reported_valid)
  {
    /* The first state is the reference, not a change */
    wsw.reported_valid = true;
    wsw.reported_live = wsw.live;
  }
  else if (wsw.live != wsw.reported_live)
  {
    wsw.reported_live = wsw.live;
    app_switch(!dp.sw, SRC_MANUAL);
  }
}
#else
void wallswitch_init(void)
{
}

void wallswitch_poll(void)
{
}
#endif

/* ------------------------------------------------------------------------ */

void app_init(void)
{
  memset(&app, 0, sizeof(app));
  app.load_state = LS_BOOT;
  app.sup_state = SUP_WAIT_MODULE;
  app.last_sec = clock_seconds();
  dp_load_defaults();
  button_init();
}

void app_poll(void)
{
  uint32_t sec = clock_seconds();
  bool factory = app.load_state >= LS_FACTORY;

  if (!factory)
  {
    if (btn.short_event && app.boot_done)
    {
      btn.short_event = false;
      app_switch(!dp.sw, SRC_MANUAL);
    }
    if (btn.long_event)
    {
      btn.long_event = false;
      app.sup_state = SUP_PAIR_ENTER;
    }
  }

  if ((app.led_word & LW_PREVIEW_MASK) && timer_expired(&app.preview_timer))
    app.led_word &= (uint16_t)~LW_PREVIEW_MASK;

  if (!factory)
    schedule_poll();
  load_poll();
  if (!factory)
    supervisor_poll();

  while (app.last_sec != sec)
  {
    app.last_sec++;
    if (!factory)
      second_tick();
  }

  render_leds();
  dp_report_poll();
  save_poll();
}
