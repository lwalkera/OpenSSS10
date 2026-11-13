/* Precomputed sound/light level tables (generated, see tools/gen_tables.py) */
#ifndef TABLES_H
#define TABLES_H

#include <stdint.h>

/* Sound and light levels run 0..LEVEL_MAX: the stock firmware's 0..100
   curves (spec §11.4, §11.5) scaled by 255 / 100 */
#define LEVEL_MAX        255

/* Stock 0..100 value to a level, and back, rounding to nearest */
#define PCT_TO_LEVEL(p)  (((p) * 255u + 50u) / 100u)
#define LEVEL_TO_PCT(l)  (((l) * 100u + 127u) / 255u)

#define SOUND_TABLE_LEN  344
#define LIGHT_TABLE_LEN  1024

extern const uint8_t sound_level_table[SOUND_TABLE_LEN];
extern const uint8_t light_level_table[LIGHT_TABLE_LEN];

#endif
