#include "libutils.h"
#include <stdio.h>
#include <string.h>

void bin2hex(char *out, const uint8_t *in, size_t len)
{
    for (size_t i = 0; i < len; i++)
        sprintf(out + i * 2, "%02x", in[i]);
    out[len * 2] = '\0';
}

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

int hex_to_bytes(const char *hex, uint8_t *bytes, size_t len)
{
    return hex2bin(bytes, hex, len);
}

int bytes_to_hex(char *hex, const uint8_t *bytes, size_t len)
{
    bin2hex(hex, bytes, len);
    return 0;
}
