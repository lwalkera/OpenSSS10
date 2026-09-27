#include "storage.h"

#include "ft32f0xx.h"
#include "record.h"

static const struct {
  uint32_t start, end;
} slots[SLOT_COUNT] = {
  { 0x0800F000, 0x0800F200 },
  { 0x0800F200, 0x0800FA00 },
  { 0x0800FE00, 0x08010000 },
};

bool flash_erase_range(uint32_t start, uint32_t end)
{
  bool ok = true;

  FLASH_Unlock();
  for (uint32_t a = start; a < end; a += FLASH_PAGE_SIZE)
  {
    if (FLASH_ErasePage(a) != FLASH_COMPLETE)
    {
      ok = false;
      break;
    }
  }
  FLASH_Lock();
  return ok;
}

bool flash_write_words(uint32_t addr, const uint32_t *data, uint32_t count)
{
  bool ok = true;

  FLASH_Unlock();
  for (uint32_t i = 0; i < count; i++, addr += 4)
  {
    if (FLASH_ProgramWord(addr, data[i]) != FLASH_COMPLETE)
    {
      ok = false;
      break;
    }
  }
  FLASH_Lock();
  return ok;
}

bool storage_read(int slot, uint32_t *out, uint32_t max, uint32_t *count)
{
  const uint32_t *data;
  uint32_t n;
  uint32_t words = (slots[slot].end - slots[slot].start) / 4;

  if (!record_parse((const uint32_t *)slots[slot].start, words, &data, &n) || n > max)
    return false;

  for (uint32_t i = 0; i < n; i++)
    out[i] = data[i];
  *count = n;
  return true;
}

bool storage_write(int slot, const uint32_t *data, uint32_t count)
{
  uint32_t addr = slots[slot].start;
  uint32_t header[2] = { RECORD_MAGIC, count };
  uint32_t sum = record_checksum(data, count);

  if ((count + RECORD_OVERHEAD) * 4 > slots[slot].end - addr)
    return false;

  return flash_erase_range(addr, slots[slot].end) &&
         flash_write_words(addr, header, 2) &&
         flash_write_words(addr + 8, data, count) &&
         flash_write_words(addr + 8 + count * 4, &sum, 1);
}
