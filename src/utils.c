/* utils.c — hex conversion and CPU feature detection helpers */

#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Convert binary bytes to null-terminated lowercase hex string */
void bin2hex(char *out, const uint8_t *in, size_t len)
{
    for (size_t i = 0; i < len; i++)
        sprintf(out + i * 2, "%02x", in[i]);
    out[len * 2] = '\0';
}

/* Parse hex string into binary buffer; returns 0 on success, -1 on invalid input */
int hex2bin(uint8_t *out, const char *in, size_t out_len)
{
    size_t len = strlen(in);
    if (len % 2 != 0 || len / 2 > out_len)
        return -1;
    for (size_t i = 0; i < len; i += 2) {
        unsigned int byte;
        if (sscanf(in + i, "%2x", &byte) != 1)
            return -1;
        out[i / 2] = (uint8_t)byte;
    }
    return 0;
}

/* Wrapper: decode hex string to bytes, delegating to hex2bin; returns 0 on success */
int hex_to_bytes(const char *hex, uint8_t *bytes, size_t len)
{
    return hex2bin(bytes, hex, len);
}

/* Wrapper: encode bytes to hex string, delegating to bin2hex; always returns 0 */
int bytes_to_hex(char *hex, const uint8_t *bytes, size_t len)
{
    bin2hex(hex, bytes, len);
    return 0;
}

#if defined(__x86_64__) || defined(__i386__)
/* Check CPU for AES-NI support using CPUID instruction; returns 1 if available */
int has_hardware_aes(void)
{
    unsigned int eax = 1, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(eax)
    );
    return (ecx >> 25) & 1;
}
#else
int has_hardware_aes(void) { return 0; }
#endif
