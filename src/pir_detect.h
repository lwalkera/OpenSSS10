/*
 * PIR motion detector over HS6605 samples: a port of the stock routine at
 * 0x08003CD0-0x08003E6A (spec §11.2), decoded from the flash dump.
 *
 * Detection follows one "lobe" of same-polarity samples. Above-threshold
 * samples are counted up to the latest peak (rise) and after it (since_peak);
 * motion is declared when rise >= T[13] and rise + since_peak >= T[15].
 * Motion is released after T[9] consecutive samples with |v| < T[5]. The
 * release counter reuses the lobe's sample counter, which still holds the
 * lobe length when motion is declared.
 *
 * The stock code keeps four lobe windows, but every rotation also clears all
 * of them, so only the current window matters. Two further branches depend on
 * flags that are never set in the stock image and are omitted.
 */
#ifndef PIR_DETECT_H
#define PIR_DETECT_H

#include <stdbool.h>
#include <stdint.h>

#define PIR_WINDOWS  4

typedef struct {
  uint32_t sum_all;        /* sum of |v| in the lobe (never read) */
  uint32_t sum_rise;       /* sum of |v| up to the latest peak */
  uint32_t sum_since;      /* sum of |v| since the latest peak */
  uint16_t count;          /* same-polarity samples; release counter while in motion */
  uint16_t rise;           /* above-threshold samples up to the latest peak */
  uint16_t since_peak;     /* above-threshold samples since the latest peak */
  uint16_t below;          /* sub-threshold samples; not cleared by a reset */
  uint16_t rise_at_peak;
  int16_t peak;
  uint8_t negative;        /* lobe polarity */
} pir_window_t;

typedef struct {
  uint8_t sensitivity;     /* DP 48, 0..2 */
  bool motion;
  uint8_t cur;
  pir_window_t w[PIR_WINDOWS];
} pir_detect_t;

/* Clear the lobe windows (stock 0x08003E84) */
void pir_detect_reset(pir_detect_t *d);

void pir_detect_set_sensitivity(pir_detect_t *d, uint8_t sensitivity);

/* Feed one sample; returns the motion flag */
bool pir_detect_sample(pir_detect_t *d, int16_t v);

#endif
