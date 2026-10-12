#pragma once
#include <stdint.h>
#include <string.h>

typedef struct { uint32_t a, b, c, d; } GuestCpuResult;
static inline GuestCpuResult guestCpuQuery(uint32_t leaf) {
    GuestCpuResult result = {0, 0, 0, 0};
    if (leaf == 0) {
        result.a = 1; result.b = 0x756e6547; result.d = 0x49656e69; result.c = 0x6c65746e;
    } else if (leaf == 1) {
        result.a = 0x00000663;
        result.d = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 15) | (1u << 23) | (1u << 25) | (1u << 26);
    } else if (leaf == 0x80000000u) result.a = 0x80000004u;
    else if (leaf >= 0x80000002u && leaf <= 0x80000004u) {
        const char brand[48] = "Android Evolved Virtual x86 CPU";
        memcpy(&result, brand + (leaf - 0x80000002u) * 16, 16);
    }
    return result;
}
