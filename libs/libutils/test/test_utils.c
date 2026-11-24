#include "libutils.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void)
{
    uint8_t bytes[] = {0xde, 0xad, 0xbe, 0xef};
    char hex[16];
    bin2hex(hex, bytes, 4);
    assert(strcmp(hex, "deadbeef") == 0);
    printf("bin2hex: OK\n");

    uint8_t out[4];
    assert(hex2bin(out, "deadbeef", 4) == 0);
    assert(memcmp(out, bytes, 4) == 0);
    printf("hex2bin: OK\n");

    assert(hex2bin(out, "invalid", 4) == -1);
    printf("hex2bin invalid: OK\n");

    printf("All utils tests passed.\n");
    return 0;
}
