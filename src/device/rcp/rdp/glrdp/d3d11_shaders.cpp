#include "d3d11_shaders.hpp"
#include "d3d11_builtin.hpp"
#ifdef HAVE_D3D11
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <mutex>
#include <regex>
#include <spirv_cross/spirv_hlsl.hpp>
#include <stdexcept>

namespace glrdp::dx11 {
namespace {
std::string cache_directory;
}
void configure_shader_cache(const std::string &directory) { cache_directory = directory; }
void check(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        char detail[64];
        std::snprintf(detail, sizeof(detail), " (HRESULT 0x%08lx)", (unsigned long)result);
        throw std::runtime_error(std::string("DX11 ") + operation + detail);
    }
}
namespace {
std::string unroll_vector_loops(std::string source) {
    const std::regex loop(
        R"(for\s*\(\s*int\s+i\s*=\s*0\s*;\s*i\s*<\s*4\s*;\s*(?:\+\+\s*i|i\s*\+\+)\s*\)\s*\{)");
    std::smatch match;
    size_t cursor = 0;
    while (std::regex_search(source.cbegin() + cursor, source.cend(), match, loop)) {
        const size_t start = cursor + match.position(), body = start + match.length();
        size_t end = body;
        unsigned depth = 1;
        while (end < source.size() && depth) {
            if (source[end] == '{')
                ++depth;
            if (source[end++] == '}')
                --depth;
        }
        if (depth)
            throw std::runtime_error("DX11 unterminated vector loop");
        const auto code = source.substr(body, end - body - 1);
        if (std::regex_search(code, std::regex(R"(\bbreak\b)"))) {
            cursor = end;
            continue;
        }
        std::string expanded;
        for (unsigned i = 0; i < 4; ++i)
            expanded += "{ const int i = " + std::to_string(i) + "; do {" + code + "} while(false); }\n";
        source.replace(start, end - start, expanded);
        cursor = start + expanded.size();
    }
    return source;
}
std::string flatten_records(std::string source) {
    std::smatch structure, block;
    const std::regex declaration(R"(struct\s+RDPPrimitive\s*\{([^}]+)\}\s*;)");
    const std::regex buffer(
        R"(layout\s*\(\s*std140\s*\)\s*uniform\s+RDPBatch\s*\{\s*RDPPrimitive\s+rdp_records\s*\[\s*(\d+)\s*\]\s*;\s*\}\s*;)");
    if (!std::regex_search(source, structure, declaration) || !std::regex_search(source, block, buffer))
        return source;
    const auto members = structure[1].str();
    struct Field {
        std::string type, name;
        unsigned offset, count;
    };
    std::vector<Field> fields;
    unsigned stride = 0;
    const std::regex field(R"(([iu]?vec4)\s+(p_\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;)");
    for (auto i = std::sregex_iterator(members.begin(), members.end(), field); i != std::sregex_iterator();
         ++i) {
        const auto &m = *i;
        const unsigned count = m[3].matched ? unsigned(std::stoul(m[3])) : 1;
        fields.push_back({m[1], m[2], stride, count});
        stride += count;
    }
    if (!stride)
        throw std::runtime_error("DX11 empty primitive record");
    const unsigned capacity = unsigned(std::stoul(block[1]));
    // FXC handles arrays of large mixed-type structs extremely slowly, even
    // with optimization disabled. The CPU already stores whole vec4 slots.
    // Use their unchanged bits through one flat array and typed bitcasts.
    source = std::regex_replace(source, buffer,
                                "layout(std140) uniform RDPBatch { vec4 glrdp_dx11_words[" +
                                    std::to_string(stride * capacity) + "]; };");
    source = std::regex_replace(source, declaration, "");
    for (const auto &f : fields) {
        const auto base = std::string(R"(rdp_records\s*\[\s*([^\[\]]+)\s*\]\s*\.\s*)") + f.name + R"(\b)";
        auto value = [&](const std::string &element) {
            auto result = "glrdp_dx11_words[($1)*" + std::to_string(stride) + "+" + std::to_string(f.offset) +
                          "+(" + element + ")]";
            if (f.type == "ivec4")
                result = "floatBitsToInt(" + result + ")";
            else if (f.type == "uvec4")
                result = "floatBitsToUint(" + result + ")";
            return "(" + result + ")";
        };
        if (f.count > 1) {
            const std::string index = R"(\s*\[\s*([^\[\]]+)\s*\])";
            source =
                std::regex_replace(source, std::regex(R"(\(\s*)" + base + R"(\s*\))" + index), value("$2"));
            source = std::regex_replace(source, std::regex(base + index), value("$2"));
            std::string array = f.type + "[" + std::to_string(f.count) + "](";
            for (unsigned i = 0; i < f.count; ++i) {
                if (i)
                    array += ",";
                array += value(std::to_string(i));
            }
            source = std::regex_replace(source, std::regex(base), "(" + array + "))");
        } else
            source = std::regex_replace(source, std::regex(base), value("0"));
    }
    if (source.find("rdp_records[") != std::string::npos)
        throw std::runtime_error("DX11 unconverted primitive access");
    return source;
}
std::string prepare(const std::string &input, EShLanguage language) {
    glslang::TShader preprocessor(language);
    auto source = input;
    source.replace(0, source.find('\n') + 1, "#version 450 core\n");
    const char *text = source.c_str();
    preprocessor.setStrings(&text, 1);
    std::string expanded;
    glslang::TShader::ForbidIncluder includer;
    if (!preprocessor.preprocess(GetDefaultResources(), 450, ENoProfile, false, false, EShMsgDefault,
                                 &expanded, includer))
        throw std::runtime_error(preprocessor.getInfoLog());
    expanded = unroll_vector_loops(flatten_records(std::move(expanded)));
    // Put loose uniforms in a named std140 block. RDPBatch already has its own
    // block; retaining that layout keeps the shared CPU primitive records exact.
    const std::regex uniform(
        R"(uniform\s+(float|int|uint|bool|[biu]?vec[234]|mat[234])\s+(\w+(?:\s*\[\s*\d+\s*\])?)\s*;)");
    std::string declarations, result;
    size_t cursor = 0;
    for (auto i = std::sregex_iterator(expanded.begin(), expanded.end(), uniform);
         i != std::sregex_iterator(); ++i) {
        const auto &m = *i;
        result.append(expanded, cursor, size_t(m.position()) - cursor);
        declarations += m[1].str() + " " + m[2].str() + ";\n";
        cursor = size_t(m.position() + m.length());
    }
    result.append(expanded, cursor, std::string::npos);
    if (!declarations.empty())
        result.insert(result.find('\n') + 1,
                      "layout(std140, binding=1) uniform GLRDPGlobals" +
                          std::string(language == EShLangVertex ? "Vertex" : "Fragment") + " {\n" +
                          declarations + "};\n");
    return result;
}
std::vector<unsigned char> hlsl(pD3DCompile compiler, const std::string &generated, const char *profile) {
    // FXC otherwise expands nested coverage and divider loops throughout the
    // ubershader, spending minutes on a single program. Keep their bounded
    // integer operations as loops; no texture derivatives occur inside them.
    auto source = std::regex_replace(generated, std::regex(R"(\bfor\s*\()"), "[loop] for (");
    // SM5 cannot dynamically address a component on the left side of an
    // int4 assignment. Four-component shade loops must remain unrolled.
    source = std::regex_replace(
        source,
        std::regex(R"(\[loop\]\s*(for\s*\(int\s+i\s*=\s*0;\s*i\s*<\s*4;\s*(?:i\s*\+\+|\+\+\s*i)\s*\)))"),
        "[unroll] $1");
    const auto started = std::chrono::steady_clock::now();
    auto checksum = [](const void *data, size_t size) {
        uint64_t hash = 1469598103934665603ull;
        const auto *bytes = static_cast<const unsigned char *>(data);
        while (size--)
            hash = (hash ^ *bytes++) * 1099511628211ull;
        return hash;
    };
    const uint64_t source_hash = checksum(source.data(), source.size());
    if (const char *directory = std::getenv("GLRDP_DX11_SHADER_DUMP")) {
        std::filesystem::create_directories(directory);
        std::ofstream(std::filesystem::path(directory) /
                          (std::to_string(source_hash) + "." + profile + ".hlsl"),
                      std::ios::binary)
            << source;
    }
    const unsigned flags =
        (std::getenv("GLRDP_DX11_NO_OPT") ? D3DCOMPILE_SKIP_OPTIMIZATION : D3DCOMPILE_OPTIMIZATION_LEVEL3) |
        (std::getenv("GLRDP_DX11_STRICT_FLOAT") ? D3DCOMPILE_IEEE_STRICTNESS : 0);
    const bool trace = std::getenv("GLRDP_DX11_SHADER_TRACE") != nullptr;
    if (!std::getenv("GLRDP_DX11_NO_BUILTINS")) {
        auto bytes = builtin_shader(source_hash, flags, profile);
        if (!bytes.empty()) {
            if (trace)
                std::fprintf(stderr, "DX11 built-in shader %s\n", profile);
            return bytes;
        }
    }
    const char *override_cache = std::getenv("GLRDP_DX11_SHADER_CACHE");
    const std::string directory = override_cache ? override_cache : cache_directory;
    std::filesystem::path cache_file;
    struct Header {
        uint64_t magic, source, data;
        uint32_t flags, bytes;
    };
    constexpr uint64_t magic = 0x3143424444505247ull;
    if (!directory.empty()) {
        cache_file = std::filesystem::path(directory) /
                     (std::to_string(source_hash) + "-" + profile + "-" + std::to_string(flags) + ".dxbc");
        std::ifstream file(cache_file, std::ios::binary);
        Header header{};
        if (file.read(reinterpret_cast<char *>(&header), sizeof(header)) && header.magic == magic &&
            header.source == source_hash && header.flags == flags && header.bytes > 0 &&
            header.bytes <= 64 * 1024 * 1024) {
            std::vector<unsigned char> bytes(header.bytes);
            if (file.read(reinterpret_cast<char *>(bytes.data()), bytes.size()) &&
                checksum(bytes.data(), bytes.size()) == header.data) {
                if (trace)
                    std::fprintf(stderr, "DX11 shader cache hit %s\n", profile);
                return bytes;
            }
        }
    }
    if (trace)
        std::fprintf(stderr, "DX11 compiling %s (%zu bytes)\n", profile, source.size());
    ComPtr<ID3DBlob> binary, errors;
    const HRESULT hr = compiler(source.data(), source.size(), "GLRDP shared shader", nullptr, nullptr, "main",
                                profile, flags, 0, &binary, &errors);
    if (trace)
        std::fprintf(stderr, "DX11 compiled %s in %.3fs\n", profile,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    if (FAILED(hr))
        throw std::runtime_error(
            errors ? std::string((const char *)errors->GetBufferPointer(), errors->GetBufferSize())
                   : "DX11 HLSL compilation failed");
    const auto *start = static_cast<const unsigned char *>(binary->GetBufferPointer());
    std::vector<unsigned char> bytes(start, start + binary->GetBufferSize());
    if (!cache_file.empty()) {
        std::error_code error;
        std::filesystem::create_directories(cache_file.parent_path(), error);
        if (!error) {
            auto temporary = cache_file;
            temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
            std::ofstream file(temporary, std::ios::binary);
            const Header header{magic, source_hash, checksum(bytes.data(), bytes.size()), flags,
                                unsigned(bytes.size())};
            file.write(reinterpret_cast<const char *>(&header), sizeof(header));
            file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            const bool written = bool(file);
            file.close();
            if (written)
                std::filesystem::rename(temporary, cache_file, error);
            if (!written || error)
                std::filesystem::remove(temporary, error);
        }
    }
    return bytes;
}
} // namespace
void compile_program(ID3D11Device *device, pD3DCompile compile, Program &program, const std::string &vertex,
                     const std::string &fragment) {
    static std::once_flag initialize;
    std::call_once(initialize, [] {
        if (!glslang::InitializeProcess())
            throw std::runtime_error("glslang initialization failed");
    });
    const std::array<std::string, 2> sources{prepare(vertex, EShLangVertex),
                                             prepare(fragment, EShLangFragment)};
    glslang::TShader vs(EShLangVertex), ps(EShLangFragment);
    std::array<glslang::TShader *, 2> shaders{&vs, &ps};
    glslang::TProgram link;
    for (unsigned i = 0; i < 2; ++i) {
        auto *s = shaders[i];
        const char *text = sources[i].c_str();
        s->setStrings(&text, 1);
        s->setEnvInput(glslang::EShSourceGlsl, i ? EShLangFragment : EShLangVertex, glslang::EShClientOpenGL,
                       450);
        s->setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
        s->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
        s->setAutoMapBindings(true);
        s->setAutoMapLocations(true);
        if (!s->parse(GetDefaultResources(), 450, false, EShMsgSpvRules))
            throw std::runtime_error(s->getInfoLog());
        link.addShader(s);
    }
    if (!link.link(EShMsgSpvRules) || !link.mapIO())
        throw std::runtime_error(link.getInfoLog());
    for (unsigned i = 0; i < 2; ++i) {
        std::vector<unsigned> spirv;
        glslang::SpvOptions options;
        options.disableOptimizer = true;
        glslang::GlslangToSpv(*link.getIntermediate(i ? EShLangFragment : EShLangVertex), spirv, &options);
        spirv_cross::CompilerHLSL compiler(std::move(spirv));
        auto &stage = program.stages[i];
        const auto resources = compiler.get_shader_resources();
        for (const auto &block : resources.uniform_buffers) {
            const bool globals = block.name.find("GLRDPGlobals") == 0 ||
                                 compiler.get_name(block.base_type_id).find("GLRDPGlobals") == 0;
            compiler.set_decoration(block.id, spv::DecorationBinding, globals ? 1 : 0);
            if (globals) {
                const auto &type = compiler.get_type(block.base_type_id);
                stage.globals.resize((compiler.get_declared_struct_size(type) + 15) & ~size_t(15));
                for (unsigned m = 0; m < type.member_types.size(); ++m)
                    stage.members.push_back({compiler.get_member_name(type.self, m),
                                             compiler.type_struct_member_offset(type, m),
                                             unsigned(compiler.get_declared_struct_member_size(type, m))});
                D3D11_BUFFER_DESC desc{};
                desc.ByteWidth = UINT(stage.globals.size());
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                check(device->CreateBuffer(&desc, nullptr, &stage.constants), "create shader constants");
            }
        }
        unsigned slot = 0;
        for (const auto &sample : resources.sampled_images) {
            const auto &type = compiler.get_type(sample.type_id);
            unsigned count = 1;
            for (auto length : type.array)
                count *= length;
            if (slot + count > D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)
                throw std::runtime_error("DX11 shader exceeds sampler limit");
            compiler.set_decoration(sample.id, spv::DecorationBinding, slot);
            stage.samples.push_back(
                {sample.name, slot, count,
                 compiler.get_type(type.image.type).basetype != spirv_cross::SPIRType::Float});
            slot += count;
        }
        auto opts = compiler.get_hlsl_options();
        opts.shader_model = 50;
        compiler.set_hlsl_options(opts);
        auto common = compiler.get_common_options();
        common.vertex.fixup_clipspace = true;
        common.vertex.flip_vert_y = true;
        compiler.set_common_options(common);
        const auto binary = hlsl(compile, compiler.compile(), i ? "ps_5_0" : "vs_5_0");
        if (i)
            check(device->CreatePixelShader(binary.data(), binary.size(), nullptr, &program.pixel),
                  "create pixel shader");
        else {
            check(device->CreateVertexShader(binary.data(), binary.size(), nullptr, &program.vertex),
                  "create vertex shader");
            // Internal surfaces keep GL's row numbering. The final frontend
            // texture is top down, so its copy uses the unflipped vertex shader.
            common.vertex.flip_vert_y = false;
            compiler.set_common_options(common);
            const auto present = hlsl(compile, compiler.compile(), "vs_5_0");
            check(
                device->CreateVertexShader(present.data(), present.size(), nullptr, &program.present_vertex),
                "create present vertex shader");
        }
    }
    program.linked = true;
}
} // namespace glrdp::dx11
#endif
