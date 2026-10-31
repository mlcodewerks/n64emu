#pragma once
#include <cstdint>

inline void glrdp_decode_combine(std::uint64_t w, int rgb0[4], int a0[4], int rgb1[4], int a1[4]) {
    rgb0[0] = int((w >> 52) & 15);
    rgb0[2] = int((w >> 47) & 31);
    a0[0] = int((w >> 44) & 7);
    a0[2] = int((w >> 41) & 7);
    rgb1[0] = int((w >> 37) & 15);
    rgb1[2] = int((w >> 32) & 31);
    rgb0[1] = int((w >> 28) & 15);
    rgb1[1] = int((w >> 24) & 15);
    a1[0] = int((w >> 21) & 7);
    a1[2] = int((w >> 18) & 7);
    rgb0[3] = int((w >> 15) & 7);
    a0[1] = int((w >> 12) & 7);
    a0[3] = int((w >> 9) & 7);
    rgb1[3] = int((w >> 6) & 7);
    a1[1] = int((w >> 3) & 7);
    a1[3] = int(w & 7);
}
