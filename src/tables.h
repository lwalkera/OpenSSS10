/* Precomputed sound/light level tables (generated, see tools/gen_tables.py) */
#ifndef TABLES_H
#define TABLES_H

#include <stdint.h>

#define SOUND_TABLE_LEN  344
#define LIGHT_TABLE_LEN  1024

extern const uint8_t sound_level_table[SOUND_TABLE_LEN];
extern const uint8_t light_level_table[LIGHT_TABLE_LEN];

#endif
