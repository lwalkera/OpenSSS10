#include "base64.h"

static const char alphabet[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint16_t base64_encode(const uint8_t *in, uint16_t len, char *out)
{
  uint16_t o = 0;

  for (uint16_t i = 0; i < len; i += 3)
  {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < len)
      v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < len)
      v |= in[i + 2];

    out[o++] = alphabet[(v >> 18) & 0x3F];
    out[o++] = alphabet[(v >> 12) & 0x3F];
    out[o++] = i + 1 < len ? alphabet[(v >> 6) & 0x3F] : '=';
    out[o++] = i + 2 < len ? alphabet[v & 0x3F] : '=';
  }
  return o;
}

static int decode_char(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

int base64_decode(const char *in, uint16_t len, uint8_t *out, uint16_t max)
{
  uint32_t acc = 0;
  int bits = 0;
  uint16_t o = 0;

  for (uint16_t i = 0; i < len; i++)
  {
    if (in[i] == '=')
      break;
    int d = decode_char(in[i]);
    if (d < 0)
      return -1;
    acc = (acc << 6) | (uint32_t)d;
    bits += 6;
    if (bits >= 8)
    {
      bits -= 8;
      if (o >= max)
        return -1;
      out[o++] = (uint8_t)(acc >> bits);
    }
  }
  return o;
}
