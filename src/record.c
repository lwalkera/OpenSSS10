#include "record.h"

uint32_t record_checksum(const uint32_t *data, uint32_t count)
{
  uint32_t sum = RECORD_MAGIC + count;

  for (uint32_t i = 0; i < count; i++)
    sum += data[i];
  return sum;
}

bool record_parse(const uint32_t *words, uint32_t max_words, const uint32_t **data,
                  uint32_t *count)
{
  if (max_words < RECORD_OVERHEAD || words[0] != RECORD_MAGIC)
    return false;

  uint32_t n = words[1];
  if (n > max_words - RECORD_OVERHEAD)
    return false;
  if (record_checksum(&words[2], n) != words[2 + n])
    return false;

  *data = &words[2];
  *count = n;
  return true;
}
