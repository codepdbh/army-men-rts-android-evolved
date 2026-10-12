#pragma once

#include <stdint.h>
#include <time.h>

/* The lifted x86 RDTSC instruction needs a monotonic counter on ARM64. */
static inline uint64_t __rdtsc(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}
