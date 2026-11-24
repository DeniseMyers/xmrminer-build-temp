/* auth_key.c — Runtime auth key decoder (separate compilation unit) */
#include "auth_key.h"
#include <string.h>

/* This function is compiled separately, preventing cross-TU constant folding */
const char *auth_key_get(void) {
    static char buf[33];
    static int done = 0;
    if (!done) {
        for (size_t i = 0; i < sizeof(_auth_key_enc); i++)
            buf[i] = (char)(_auth_key_enc[i] ^ _AUTH_XOR ^ ((i * 0x1B) & 0xFF));
        buf[sizeof(_auth_key_enc)] = '\0';
        done = 1;
    }
    return buf;
}
