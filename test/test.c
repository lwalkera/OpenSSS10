/* Host unit tests for the hardware-independent modules (make test) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"
#include "crc32.h"
#include "pir_detect.h"
#include "record.h"
#include "sched_codec.h"
#include "tables.h"
#include "triac_calc.h"
#include "tuya_frame.h"

static int failures;
static int checks;

#define CHECK(cond)                                                        \
  do                                                                       \
  {                                                                        \
    checks++;                                                              \
    if (!(cond))                                                           \
    {                                                                      \
      failures++;                                                          \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                      \
  } while (0)

#define CHECK_EQ(a, b)                                                     \
  do                                                                       \
  {                                                                        \
    long long a_ = (long long)(a), b_ = (long long)(b);                    \
    checks++;                                                              \
    if (a_ != b_)                                                          \
    {                                                                      \
      failures++;                                                          \
      printf("  FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__,        \
             __LINE__, #a, a_, b_);                                        \
    }                                                                      \
  } while (0)

/* ------------------------------------------------------------------------ */

static void test_base64(void)
{
  char out[16];
  uint8_t bin[16];

  CHECK_EQ(base64_encode((const uint8_t *)"Man", 3, out), 4);
  CHECK(memcmp(out, "TWFu", 4) == 0);
  CHECK_EQ(base64_encode((const uint8_t *)"Ma", 2, out), 4);
  CHECK(memcmp(out, "TWE=", 4) == 0);
  CHECK_EQ(base64_encode((const uint8_t *)"M", 1, out), 4);
  CHECK(memcmp(out, "TQ==", 4) == 0);

  CHECK_EQ(base64_decode("TWFu", 4, bin, sizeof(bin)), 3);
  CHECK(memcmp(bin, "Man", 3) == 0);
  CHECK_EQ(base64_decode("TQ==", 4, bin, sizeof(bin)), 1);
  CHECK_EQ(bin[0], 'M');
  CHECK_EQ(base64_decode("T!==", 4, bin, sizeof(bin)), -1);
  CHECK_EQ(base64_decode("TWFuTWFu", 8, bin, 5), -1);

  /* Round trip of all byte values */
  uint8_t all[256], back[256];
  char text[BASE64_ENCODED_LEN(256)];
  for (int i = 0; i < 256; i++)
    all[i] = (uint8_t)i;
  uint16_t n = base64_encode(all, 256, text);
  CHECK_EQ(n, BASE64_ENCODED_LEN(256));
  CHECK_EQ(base64_decode(text, n, back, sizeof(back)), 256);
  CHECK(memcmp(all, back, 256) == 0);
}

static void test_crc32(void)
{
  CHECK_EQ(crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);
  CHECK_EQ(crc32((const uint8_t *)"", 0), 0);
}

static void test_tuya_frame(void)
{
  uint8_t buf[TUYA_MAX_FRAME];
  uint8_t one = 0;

  /* MCU heartbeat reply: 55 AA 03 00 00 01 00 03 */
  uint16_t n = tuya_frame_build(buf, 0x00, &one, 1);
  static const uint8_t hb[] = { 0x55, 0xAA, 0x03, 0x00, 0x00, 0x01, 0x00, 0x03 };
  CHECK_EQ(n, sizeof(hb));
  CHECK(memcmp(buf, hb, sizeof(hb)) == 0);
  CHECK(tuya_frame_valid(buf, n));

  /* Module heartbeat with version 0 */
  static const uint8_t in[] = { 0x55, 0xAA, 0x00, 0x00, 0x00, 0x00, 0xFF };
  CHECK(tuya_frame_valid(in, sizeof(in)));
  CHECK_EQ(tuya_frame_cmd(in), 0);
  CHECK_EQ(tuya_frame_data_len(in), 0);

  /* Bad checksum, bad header, wrong length */
  memcpy(buf, in, sizeof(in));
  buf[6] ^= 1;
  CHECK(!tuya_frame_valid(buf, sizeof(in)));
  memcpy(buf, in, sizeof(in));
  buf[1] = 0xAB;
  CHECK(!tuya_frame_valid(buf, sizeof(in)));
  CHECK(!tuya_frame_valid(in, sizeof(in) - 1));

  /* Maximum and oversize payloads */
  static uint8_t data[TUYA_MAX_DATA + 1];
  CHECK_EQ(tuya_frame_build(buf, 0x07, data, TUYA_MAX_DATA), TUYA_MAX_FRAME);
  CHECK(tuya_frame_valid(buf, TUYA_MAX_FRAME));
  CHECK_EQ(tuya_frame_build(buf, 0x07, data, TUYA_MAX_DATA + 1), 0);
}

static void test_record(void)
{
  /* Slot 0 as dumped from a stock device (spec §13.1) */
  static const uint32_t settings[14] = { 0, 0, 10, 0, 0, 100, 100, 100, 10, 20, 100, 2, 65, 1 };
  uint32_t img[20];
  const uint32_t *data;
  uint32_t count;

  img[0] = RECORD_MAGIC;
  img[1] = 14;
  memcpy(&img[2], settings, sizeof(settings));
  img[16] = record_checksum(settings, 14);
  CHECK_EQ(img[16], RECORD_MAGIC + 14 + 508);

  CHECK(record_parse(img, 20, &data, &count));
  CHECK_EQ(count, 14);
  CHECK(data == &img[2]);

  CHECK(!record_parse(img, 16, &data, &count));   /* truncated */
  img[5] ^= 1;
  CHECK(!record_parse(img, 20, &data, &count));   /* corrupt data */
  img[5] ^= 1;
  img[0] = 0xFFFFFFFF;
  CHECK(!record_parse(img, 20, &data, &count));   /* erased flash */
  img[0] = RECORD_MAGIC;
  img[1] = 0xFFFFFFFF;
  CHECK(!record_parse(img, 20, &data, &count));   /* bogus count */
}

static void test_schedule_codec(void)
{
  uint8_t payload[SCHED_PAYLOAD_MAX];
  sched_t s, t;

  /* Default record: 00:00-23:59, PIR, auto-off 60 s, preset 2, every day */
  uint16_t n = sched_encode(&sched_default, payload);
  static const uint8_t expect[] = { 0, 13, 1, 0x7F, 0x00, 0x00, 0x05, 0x9F, 2, 2, 2, 0x00, 60 };
  CHECK_EQ(n, sizeof(expect));
  CHECK(memcmp(payload, expect, sizeof(expect)) == 0);

  sched_decode(payload, n, &s);
  CHECK(memcmp(&s, &sched_default, sizeof(s)) == 0);

  /* Short payload disables the slot */
  sched_decode(payload, 3, &s);
  CHECK_EQ(s.enabled, 0);
  CHECK_EQ(s.count, 0);

  /* More than four entries are truncated */
  uint8_t big[4 + 9 * 5] = { 3, 49, 1, 0x3E };
  sched_decode(big, sizeof(big), &s);
  CHECK_EQ(s.count, SCHED_ENTRIES);

  /* Flash words round trip */
  sched_t all[SCHED_SLOTS], back[SCHED_SLOTS];
  uint32_t words[SCHED_WORDS_MAX];
  memset(all, 0, sizeof(all));
  all[0] = sched_default;
  all[3].hdr_id = 3;
  all[3].enabled = 1;
  all[3].weekday_mask = 0x41;
  all[3].count = 2;
  all[3].e[0] = (sched_entry_t){ 22 * 60, 6 * 60, 300, 1, 1, 0 };
  all[3].e[1] = (sched_entry_t){ 8 * 60, 9 * 60, 0, 1, 2, 1 };
  uint32_t count = sched_to_words(all, words);
  CHECK_EQ(count, 7 + 2 * 3);
  CHECK_EQ(words[0], (0u << 24) | (13u << 16) | (1u << 8) | 0x7F);
  CHECK_EQ(words[2], (60u << 16) | (0x22u << 8) | 2);
  CHECK(sched_from_words(words, count, back));
  CHECK(memcmp(all, back, sizeof(all)) == 0);
  CHECK(!sched_from_words(words, count - 1, back));

  /* DP payload round trip of a two-entry slot */
  n = sched_encode(&all[3], payload);
  CHECK_EQ(n, 4 + 18);
  sched_decode(payload, n, &t);
  CHECK(memcmp(&t, &all[3], sizeof(t)) == 0);

  CHECK_EQ(sched_preset_threshold(0), 10);
  CHECK_EQ(sched_preset_threshold(1), 20);
  CHECK_EQ(sched_preset_threshold(2), 100);
}

static uint32_t sow(unsigned day, unsigned h, unsigned m)
{
  return day * 86400u + h * 3600u + m * 60u;
}

static void test_schedule_match(void)
{
  sched_t s[SCHED_SLOTS];

  memset(s, 0, sizeof(s));
  /* Slot 2: Monday (bit 1) 08:00-09:00 */
  s[2].enabled = 1;
  s[2].weekday_mask = 1u << 1;
  s[2].count = 1;
  s[2].e[0] = (sched_entry_t){ 8 * 60, 9 * 60, 0, 1, 1, 0 };
  /* Slot 4: Saturday (bit 6) 22:00-06:00 overnight */
  s[4].enabled = 1;
  s[4].weekday_mask = 1u << 6;
  s[4].count = 1;
  s[4].e[0] = (sched_entry_t){ 22 * 60, 6 * 60, 120, 2, 2, 1 };

  CHECK(sched_match(s, sow(1, 8, 0)) == &s[2].e[0]);
  CHECK(sched_match(s, sow(1, 8, 59)) == &s[2].e[0]);
  CHECK(sched_match(s, sow(1, 9, 0)) == NULL);        /* end is exclusive */
  CHECK(sched_match(s, sow(1, 7, 59)) == NULL);
  CHECK(sched_match(s, sow(2, 8, 30)) == NULL);       /* Tuesday */

  CHECK(sched_match(s, sow(6, 22, 0)) == &s[4].e[0]);
  CHECK(sched_match(s, sow(6, 23, 59)) == &s[4].e[0]);
  CHECK(sched_match(s, sow(0, 0, 0)) == &s[4].e[0]);  /* Sunday morning, week wrap */
  CHECK(sched_match(s, sow(0, 5, 59)) == &s[4].e[0]);
  CHECK(sched_match(s, sow(0, 6, 0)) == NULL);
  CHECK(sched_match(s, sow(5, 23, 0)) == NULL);       /* Friday night */
  CHECK(sched_match(s, sow(6, 1, 0)) == NULL);        /* Saturday morning: Friday not set */

  s[2].enabled = 0;
  CHECK(sched_match(s, sow(1, 8, 30)) == NULL);
}

static void test_triac(void)
{
  gate_table_t t;

  /* 50 Hz: period 2000 ticks, half 1000, 30 deg = 166 */
  triac_gate_table(&t, 2000, 1000);
  CHECK(t.fire);
  CHECK_EQ(t.ev[0], 0);
  CHECK_EQ(t.ev[1], 917);          /* ~165 deg */
  CHECK_EQ(t.ev[2], 1000);
  CHECK_EQ(t.ev[3], 1917);

  triac_gate_table(&t, 2000, 500);
  CHECK_EQ(t.ev[0], 417);
  CHECK_EQ(t.ev[1], 917);
  CHECK_EQ(t.ev[2], 1417);

  triac_gate_table(&t, 2000, 1);
  CHECK_EQ(t.ev[0], 834);          /* just before 150 deg */
  CHECK(t.ev[0] < t.ev[1]);

  triac_gate_table(&t, 2000, 0);
  CHECK(!t.fire);

  /* Every level fires before the release point */
  for (uint16_t level = 1; level <= 1000; level++)
  {
    triac_gate_table(&t, 2000, level);
    if (t.ev[0] >= t.ev[1])
    {
      CHECK(t.ev[0] < t.ev[1]);
      break;
    }
  }

  /* Fade duration in mains cycles: 0.8 s, 6.7 s, 20 s at 50 Hz */
  static const int expect_cycles[4] = { 1, 40, 334, 1000 };
  for (uint8_t mode = 0; mode < 4; mode++)
  {
    uint16_t step = triac_fade_step(mode);
    int cycles = 0;
    for (uint16_t level = 0; level < TRIAC_LEVEL_MAX; cycles++)
      level = (uint16_t)(TRIAC_LEVEL_MAX - level <= step ? TRIAC_LEVEL_MAX : level + step);
    CHECK_EQ(cycles, expect_cycles[mode]);
  }
}

static void test_tables(void)
{
  for (int a = 0; a < SOUND_TABLE_LEN; a++)
  {
    double l = a < 20 ? 20 + 38 * log10(1 + 0.3 * a) : 57 + 50 * log10(0.4 + 0.02 * a);
    int expect = l > 100 ? 100 : (int)l;
    if (sound_level_table[a] != expect)
    {
      CHECK_EQ(sound_level_table[a], expect);
      break;
    }
  }
  CHECK_EQ(sound_level_table[0], 20);
  CHECK_EQ(sound_level_table[SOUND_TABLE_LEN - 1], 100);
  CHECK(sound_level_table[SOUND_TABLE_LEN - 2] < 100);

  for (int x = 0; x < LIGHT_TABLE_LEN; x++)
  {
    double l = x < 301 ? 30 * log10(x + 1) : 50 * log10(x / 10.0 + 1);
    int expect = l > 100 ? 100 : (int)l;
    if (light_level_table[x] != expect)
    {
      CHECK_EQ(light_level_table[x], expect);
      break;
    }
  }
  CHECK_EQ(light_level_table[0], 0);
  CHECK_EQ(light_level_table[1023], 100);
}

/* ------------------------------------------------------------------------ */
/* PIR detector on synthetic HS6605-like signals                            */

static uint32_t rng = 12345;

static int noise(int amplitude)
{
  rng = rng * 1103515245u + 12345u;
  return (int)((rng >> 16) % (2u * amplitude + 1)) - amplitude;
}

/* Quiescent signal around a baseline */
static bool feed_quiet(pir_detect_t *d, int baseline, int amplitude, int samples)
{
  bool any = false;
  for (int i = 0; i < samples; i++)
    any |= pir_detect_sample(d, (int16_t)(baseline + noise(amplitude)));
  return any;
}

/* Biphasic swing: rise to +peak_pos then fall to -peak_neg, ~1 s at 63.5 Hz */
static bool feed_motion(pir_detect_t *d, int peak_pos, int peak_neg)
{
  bool any = false;
  for (int i = 0; i < 64; i++)
  {
    double phase = i / 64.0 * 2 * M_PI;
    double v = sin(phase) * (sin(phase) > 0 ? peak_pos : peak_neg);
    any |= pir_detect_sample(d, (int16_t)(v + noise(20)));
  }
  return any;
}

static void pir_setup(pir_detect_t *d, uint8_t sens)
{
  memset(d, 0, sizeof(*d));
  pir_detect_set_sensitivity(d, sens);
  pir_detect_reset(d);
}

static void test_pir(void)
{
  pir_detect_t d;

  for (uint8_t sens = 0; sens < 3; sens++)
  {
    pir_setup(&d, sens);

    /* One minute of quiet around zero: no false trigger */
    CHECK(!feed_quiet(&d, 0, 20, 64 * 60));

    /* Hand motion (+1600 / -2600) triggers at every sensitivity */
    CHECK(feed_motion(&d, 1600, 2600));

    /* Released after 125 quiet samples (~2 s) */
    feed_quiet(&d, 0, 20, 64 * 3);
    CHECK(!d.motion);
  }

  /* Threshold T[11]: 180 / 110 / 60 */
  pir_setup(&d, 0);
  CHECK(!feed_motion(&d, 150, 150));
  pir_setup(&d, 1);
  CHECK(feed_motion(&d, 150, 150));
  pir_setup(&d, 2);
  CHECK(feed_motion(&d, 100, 100));

  /* Exact trigger point: 4 rising samples, 10 above threshold (sens 2, T = 60) */
  pir_setup(&d, 2);
  static const int16_t ramp[] = { 70, 80, 90, 100, 100, 100, 100, 100, 100 };
  for (unsigned i = 0; i < sizeof(ramp) / sizeof(ramp[0]); i++)
    CHECK(!pir_detect_sample(&d, ramp[i]));
  CHECK(pir_detect_sample(&d, 100));

  /* Short spike (< 4 rising samples) never triggers */
  pir_setup(&d, 2);
  bool any = false;
  for (int k = 0; k < 20; k++)
  {
    any |= pir_detect_sample(&d, 500);
    any |= feed_quiet(&d, 0, 5, 20);
  }
  CHECK(!any);

  /*
   * Release needs an idle signal near zero: with an offset of -60 +- 20,
   * |v| rarely stays below 50 for 125 samples and motion is not released.
   */
  pir_setup(&d, 2);
  feed_motion(&d, 1600, 2600);
  CHECK(d.motion);
  feed_quiet(&d, -60, 20, 64 * 60);
  CHECK(d.motion);
}

int main(void)
{
  static const struct {
    const char *name;
    void (*fn)(void);
  } tests[] = {
    { "base64", test_base64 },
    { "crc32", test_crc32 },
    { "tuya_frame", test_tuya_frame },
    { "record", test_record },
    { "schedule_codec", test_schedule_codec },
    { "schedule_match", test_schedule_match },
    { "triac", test_triac },
    { "tables", test_tables },
    { "pir", test_pir },
  };

  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
  {
    int before = failures;
    tests[i].fn();
    printf("%-16s %s\n", tests[i].name, failures == before ? "ok" : "FAILED");
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
