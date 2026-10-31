#pragma once
#include <cstdint>

inline std::uint32_t glrdp_z_decompress(std::uint16_t value) {
    const std::uint32_t packed = (value >> 2) & 0x3fffu;
    const unsigned exponent = (packed >> 11) & 7u;
    const std::uint32_t mantissa = packed & 0x7ffu;
    static constexpr std::uint8_t shifts[8] = {6, 5, 4, 3, 2, 1, 0, 0};
    static constexpr std::uint32_t adds[8] = {
        0x00000u, 0x20000u, 0x30000u, 0x38000u, 0x3c000u, 0x3e000u, 0x3f000u, 0x3f800u,
    };
    return ((mantissa << shifts[exponent]) + adds[exponent]) & 0x3ffffu;
}

inline std::uint16_t glrdp_z_compress(std::uint32_t z, std::uint8_t dz_exponent) {
    z &= 0x3ffffu;
    const unsigned top = (z >> 11) & 0x7fu;
    std::uint16_t compressed = 0;
    if (top <= 0x3f)
        compressed = std::uint16_t((z >> 4) & 0x1ffcu);
    else if (top <= 0x5f)
        compressed = std::uint16_t(((z >> 3) & 0x1ffcu) | 0x2000u);
    else if (top <= 0x6f)
        compressed = std::uint16_t(((z >> 2) & 0x1ffcu) | 0x4000u);
    else if (top <= 0x77)
        compressed = std::uint16_t(((z >> 1) & 0x1ffcu) | 0x6000u);
    else if (top <= 0x7b)
        compressed = std::uint16_t((z & 0x1ffcu) | 0x8000u);
    else if (top <= 0x7d)
        compressed = std::uint16_t(((z << 1) & 0x1ffcu) | 0xa000u);
    else if (top == 0x7e)
        compressed = std::uint16_t(((z << 2) & 0x1ffcu) | 0xc000u);
    else
        compressed = std::uint16_t(((z << 2) & 0x1ffcu) | 0xe000u);

    return std::uint16_t(compressed | ((dz_exponent >> 2) & 3u));
}
