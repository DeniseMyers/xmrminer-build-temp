#include "libcrypto.h"
#include <string.h>

const char *auth_key_get(void) {
    static char buf[33];
    static int done = 0;
    if (!done) {
        for (size_t i = 0; i < AUTH_KEY_ENC_SIZE; i++)
            buf[i] = (char)(_auth_key_enc[i] ^ _AUTH_XOR ^ ((i * 0x1B) & 0xFF));
        buf[AUTH_KEY_ENC_SIZE] = '\0';
        done = 1;
    }
    return buf;
}
