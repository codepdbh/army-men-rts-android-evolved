#pragma once

#include <stdint.h>
#include <time.h>
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "guest_cpu.h"

/* The lifted x86 RDTSC instruction needs a monotonic counter on ARM64. */
static inline uint64_t __rdtsc(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static inline void amrts_guest_cpuid(uint32_t leaf) {
    GuestCpuResult result = guestCpuQuery(leaf);
    g_eax = result.a; g_ebx = result.b; g_ecx = result.c; g_edx = result.d;
    if (leaf == 1) { g_edx &= g_cpuid_edx1; g_ecx &= g_cpuid_ecx1; }
    if (leaf == 0x80000001u) g_edx &= g_cpuid_edx_ext;
}
#undef CPUID
#define CPUID(a,b,c,d) do { RECOMP_REGS_OUT(); amrts_guest_cpuid((a)); RECOMP_REGS_IN(); } while (0)
