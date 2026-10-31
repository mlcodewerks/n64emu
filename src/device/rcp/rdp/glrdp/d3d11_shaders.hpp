#pragma once
#ifdef HAVE_D3D11
#include <array>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

namespace glrdp::dx11 {
template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
struct UniformMember {
    std::string name;
    unsigned offset, size;
};
struct SampleBinding {
    std::string name;
    unsigned slot, count;
    bool integer;
};
struct ShaderStage {
    std::vector<UniformMember> members;
    std::vector<SampleBinding> samples;
    std::vector<unsigned char> globals;
    // Cached programs share the GPU buffer. Its uploaded bytes must be shared
    // too, while each linked program keeps its own GL-style uniform values.
    std::shared_ptr<std::vector<unsigned char>> uploaded = std::make_shared<std::vector<unsigned char>>();
    ComPtr<ID3D11Buffer> constants;
};
struct Program {
    ComPtr<ID3D11VertexShader> vertex, present_vertex;
    ComPtr<ID3D11PixelShader> pixel;
    std::array<ShaderStage, 2> stages;
    std::vector<std::string> locations;
    std::unordered_map<std::string, unsigned> texture_units;
    std::string error;
    bool linked = false;
};
void compile_program(ID3D11Device *device, pD3DCompile compiler, Program &program, const std::string &vertex,
                     const std::string &fragment);
void check(HRESULT result, const char *operation);
void configure_shader_cache(const std::string &directory);
} // namespace glrdp::dx11
#endif
