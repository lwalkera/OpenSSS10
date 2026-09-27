/* Standard-alphabet base64 with '=' padding */
#ifndef BASE64_H
#define BASE64_H

#include <stdint.h>

#define BASE64_ENCODED_LEN(n)  ((((n) + 2) / 3) * 4)

/* Returns the encoded length (no terminator written) */
uint16_t base64_encode(const uint8_t *in, uint16_t len, char *out);

/* Returns the decoded length, or -1 on invalid input or overflow */
int base64_decode(const char *in, uint16_t len, uint8_t *out, uint16_t max);

#endif
