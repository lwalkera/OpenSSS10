#include "pir_detect.h"

/*
 * Per-sensitivity parameter rows from the stock image (0x08006DCA), 37 int16
 * each; the rows differ only in T[11]. Unlisted fields are unused.
 */
static const int16_t params[3][37] = {
  { 60, 0, 0, 62, 0, 50, 20, 0, 0, 125, 0, 180, 20, 4, 5, 10, 0, 4, 5, 10, 0, 6, 10, 18, 0,
    4, 5, 10, 0, 6, 10, 18, 0, 4, 8, 14, 0 },
  { 60, 0, 0, 62, 0, 50, 20, 0, 0, 125, 0, 110, 20, 4, 5, 10, 0, 4, 5, 10, 0, 6, 10, 18, 0,
    4, 5, 10, 0, 6, 10, 18, 0, 4, 8, 14, 0 },
  { 60, 0, 0, 62, 0, 50, 20, 0, 0, 125, 0, 60, 20, 4, 5, 10, 0, 4, 5, 10, 0, 6, 10, 18, 0,
    4, 5, 10, 0, 6, 10, 18, 0, 4, 8, 14, 0 },
};

#define T_RELEASE_LEVEL   5    /* |v| below this counts as quiet */
#define T_RELEASE_COUNT   9    /* quiet samples before motion is released */
#define T_AMPLITUDE      11    /* lobe threshold; also the sub-threshold run limit */
#define T_RISE           13    /* minimum samples up to the latest peak */
#define T_EVENTS         15    /* minimum above-threshold samples in the lobe */

#define POLARITY_MIN      20   /* opposite-polarity |v| that ends a lobe */
#define LOBE_MIN_SAMPLES  30   /* lobe length needed to move to a new window */
#define GAP_SAMPLES        3   /* a longer sub-threshold gap restarts peak tracking */

static void clear_window(pir_window_t *w)
{
  /* sum_all and below survive, as in the stock code */
  w->sum_rise = 0;
  w->sum_since = 0;
  w->count = 0;
  w->rise = 0;
  w->since_peak = 0;
  w->rise_at_peak = 0;
  w->peak = 0;
  w->negative = 0;
}

void pir_detect_reset(pir_detect_t *d)
{
  for (int i = 0; i < PIR_WINDOWS; i++)
    clear_window(&d->w[i]);
}

void pir_detect_set_sensitivity(pir_detect_t *d, uint8_t sensitivity)
{
  d->sensitivity = sensitivity > 2 ? 2 : sensitivity;
}

/* A sub-threshold sample: too many in a row end the lobe */
static void below_threshold(pir_detect_t *d, pir_window_t *w, int16_t amplitude)
{
  if (++w->below > amplitude)
    pir_detect_reset(d);
}

bool pir_detect_sample(pir_detect_t *d, int16_t v)
{
  const int16_t *T = params[d->sensitivity];
  pir_window_t *w = &d->w[d->cur];
  uint8_t negative = v < 0;
  int16_t a = (int16_t)(negative ? -v : v);

  if (d->motion)
  {
    if (a < T[T_RELEASE_LEVEL])
    {
      if (++w->count >= (uint16_t)T[T_RELEASE_COUNT])
      {
        d->motion = false;
        pir_detect_reset(d);
      }
    }
    else
    {
      w->count = 0;
    }
    return d->motion;
  }

  if (w->count == 0)
  {
    if (a < T[T_AMPLITUDE])
      return false;
    w->negative = negative;
  }

  if (w->negative != negative)
  {
    if (a < POLARITY_MIN)
      below_threshold(d, w, T[T_AMPLITUDE]);
    else if (w->rise + w->since_peak < LOBE_MIN_SAMPLES)
      pir_detect_reset(d);
    else
    {
      d->cur = (uint8_t)((d->cur + 1) % PIR_WINDOWS);
      pir_detect_reset(d);
    }
    return false;
  }

  w->sum_all += (uint32_t)(int32_t)a;   /* -32768 stays negative, as in stock */
  w->count++;

  if (a < T[T_AMPLITUDE])
  {
    below_threshold(d, w, T[T_AMPLITUDE]);
    return false;
  }

  if (w->below > GAP_SAMPLES)
  {
    w->sum_rise = 0;
    w->sum_since = 0;
    w->rise = 0;
    w->since_peak = 0;
    w->rise_at_peak = 0;
    w->peak = 0;
  }
  w->below = 0;

  w->since_peak++;
  w->sum_since += (uint32_t)(int32_t)a;
  if (w->peak < a)
  {
    /* New peak: everything so far counts as rise */
    w->rise = (uint16_t)(w->rise + w->since_peak);
    w->sum_rise += w->sum_since;
    w->since_peak = 0;
    w->sum_since = 0;
    w->peak = a;
    w->rise_at_peak = w->rise;
  }

  if (w->rise >= (uint16_t)T[T_RISE] && w->rise + w->since_peak >= T[T_EVENTS])
    d->motion = true;

  return d->motion;
}
