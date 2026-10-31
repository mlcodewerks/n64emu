#include "d3d11_builtin.hpp"
#ifdef HAVE_D3D11
#include "d3d11_shader_data.hpp"
#include <cstring>
#include <stdexcept>
namespace glrdp::dx11 {
namespace {
std::vector<unsigned char> base64(const generated::Entry &entry) {
    std::vector<unsigned char> result;
    result.reserve(entry.compressed);
    unsigned bits = 0, count = 0;
    for (const char *p = entry.base64; *p && *p != '='; ++p) {
        const unsigned c = *p;
        const unsigned value = c >= 'A' && c <= 'Z'   ? c - 'A'
                               : c >= 'a' && c <= 'z' ? c - 'a' + 26
                               : c >= '0' && c <= '9' ? c - '0' + 52
                               : c == '+'             ? 62
                               : c == '/'             ? 63
                                                      : 64;
        if (value > 63)
            throw std::runtime_error("DX11 invalid built-in shader encoding");
        bits = (bits << 6) | value;
        count += 6;
        if (count >= 8) {
            count -= 8;
            result.push_back(static_cast<unsigned char>(bits >> count));
        }
    }
    if (result.size() != entry.compressed)
        throw std::runtime_error("DX11 truncated built-in shader");
    return result;
}
std::vector<unsigned char> unpack(const generated::Entry &entry) {
    const auto source = base64(entry);
    std::vector<unsigned char> result(entry.bytes);
    size_t input = 0, output = 0;
    auto length = [&](size_t initial) {
        size_t total = initial;
        if (initial == 15) {
            unsigned extra;
            do {
                if (input == source.size())
                    throw std::runtime_error("DX11 truncated shader block");
                extra = source[input++];
                total += extra;
                if (total > entry.bytes)
                    throw std::runtime_error("DX11 invalid shader block length");
            } while (extra == 255);
        }
        return total;
    };
    while (input < source.size()) {
        const unsigned token = source[input++];
        const size_t literals = length(token >> 4);
        if (literals > source.size() - input || literals > result.size() - output)
            throw std::runtime_error("DX11 invalid shader literals");
        std::memcpy(result.data() + output, source.data() + input, literals);
        input += literals;
        output += literals;
        if (input == source.size())
            break;
        if (source.size() - input < 2)
            throw std::runtime_error("DX11 missing shader match");
        const size_t distance = source[input] | (unsigned(source[input + 1]) << 8);
        input += 2;
        const size_t match = length(token & 15) + 4;
        if (!distance || distance > output || match > result.size() - output)
            throw std::runtime_error("DX11 invalid shader match");
        for (size_t i = 0; i < match; ++i) {
            result[output] = result[output - distance];
            ++output;
        }
    }
    uint64_t checksum = 1469598103934665603ull;
    for (unsigned char byte : result)
        checksum = (checksum ^ byte) * 1099511628211ull;
    if (output != result.size() || checksum != entry.checksum)
        throw std::runtime_error("DX11 built-in shader checksum failed");
    return result;
}
} // namespace
std::vector<unsigned char> builtin_shader(uint64_t source, unsigned flags, const char *profile) {
    for (const auto &entry : generated::shaders)
        if (entry.source == source && entry.flags == flags && !std::strcmp(entry.profile, profile))
            return unpack(entry);
    return {};
}
} // namespace glrdp::dx11
#endif
