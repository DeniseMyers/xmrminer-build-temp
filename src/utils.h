/* utils.h — hex conversion and CPU feature detection helpers */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>

/* Convert binary bytes to null-terminated hex string */
void bin2hex(char *out, const uint8_t *in, size_t len);

/* Parse hex string into binary bytes; returns 0 on success, -1 on error */
int hex2bin(uint8_t *out, const char *in, size_t out_len);

/* Wrapper: decode hex string to bytes (calls hex2bin); returns 0 on success */
int hex_to_bytes(const char *hex, uint8_t *bytes, size_t len);

/* Wrapper: encode bytes to hex string (calls bin2hex); always returns 0 */
int bytes_to_hex(char *hex, const uint8_t *bytes, size_t len);

/* Check CPU for AES-NI support via CPUID; returns 1 if available, 0 otherwise */
int has_hardware_aes(void);

#endif
