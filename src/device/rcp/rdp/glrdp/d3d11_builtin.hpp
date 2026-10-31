#pragma once
#ifdef HAVE_D3D11
#include <cstdint>
#include <vector>
namespace glrdp::dx11 {
std::vector<unsigned char> builtin_shader(std::uint64_t source, unsigned flags, const char *profile);
}
#endif
