/* Application state machines (spec §8.5, §10.4-10.6, §13.1, §14) */
#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

/* Switch trigger sources (spec §14.1) */
enum {
  SRC_MANUAL = 0,   /* DP 1, button, wall switch */
  SRC_TIMER = 1,    /* schedule, countdown */
  SRC_SENSOR = 2,   /* PIR/sound turn-on, inactivity auto-off */
  SRC_TEST = 3,     /* DP 115 test mode entered */
};

/* LED output-request word (spec §10.4) */
#define LW_COLOR_MASK    0x003Fu
#define LW_WHITE         0x0001u   /* white at DP 101 level */
#define LW_CYAN          0x001Cu   /* cyan at DP 102 level */
#define LW_FACTORY       0x0020u   /* bits 4-5 = 10: steady 100 % on bits 0-3 */
#define LW_PAIR_MASK     0x00C0u
#define LW_PAIR_BLINK    0x0040u   /* white blink forever (pairing) */
#define LW_PAIR_LOST     0x0080u   /* yellow breathe (connection lost) */
#define LW_PREVIEW_MASK  0x3F00u
#define LW_SENSOR_TEST   0x8000u   /* PB13 breathe (DP 115 waiting) */

void app_init(void);

/* Load settings, schedules and the tag from flash (init step 19) */
void app_load(void);

void app_poll(void);
void button_poll(void);
void wallswitch_init(void);
void wallswitch_poll(void);

/* Switch the load (spec §14.1) */
void app_switch(bool on, uint8_t src);

/* Register the Tuya callbacks */
void app_register_tuya(void);

/* Used by the factory test */
void app_set_led_word(uint16_t word);
void app_load_defaults(void);
void app_save_all(void);
bool button_held(void);
bool button_take_press(void);   /* short press since the last call */

#endif
