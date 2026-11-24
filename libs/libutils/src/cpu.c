#include "libutils.h"

#if defined(__x86_64__) || defined(__i386__)
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
