// All bindings are private to one outer GLStateGuard. The frontend can change
// arbitrary GL state between guards; no cached answer survives that boundary.
#include <array>
namespace {
struct CachedBinding {
    GLuint value = 0;
    bool valid = false;
};
struct GLBindingCache {
    bool enabled = false;
    CachedBinding program, vao, active, draw_fbo, read_fbo;
    std::array<std::array<CachedBinding, 2>, 12> textures{};
    std::array<CachedBinding, 4> buffers{}, capabilities{};
    struct Image {
        std::array<GLuint, 6> values{};
        bool valid = false;
    };
    std::array<Image, 3> images{};
};
thread_local GLBindingCache binding_cache;
decltype(glad_glUseProgram) uncached_use_program;
decltype(glad_glActiveTexture) uncached_active_texture;
decltype(glad_glBindTexture) uncached_bind_texture;
decltype(glad_glBindBuffer) uncached_bind_buffer;
decltype(glad_glBindBufferBase) uncached_bind_buffer_base;
decltype(glad_glBindBufferRange) uncached_bind_buffer_range;
decltype(glad_glBindVertexArray) uncached_bind_vao;
decltype(glad_glBindFramebuffer) uncached_bind_fbo;
decltype(glad_glEnable) uncached_enable;
decltype(glad_glDisable) uncached_disable;
decltype(glad_glDeleteTextures) uncached_delete_textures;
decltype(glad_glDeleteBuffers) uncached_delete_buffers;
decltype(glad_glDeleteVertexArrays) uncached_delete_vaos;
decltype(glad_glDeleteFramebuffers) uncached_delete_fbos;
decltype(glad_glDeleteProgram) uncached_delete_program;
decltype(glad_glBindImageTexture) uncached_bind_image;
bool same_binding(CachedBinding &binding, GLuint value) {
    if (!binding_cache.enabled)
        return false;
    if (binding.valid && binding.value == value)
        return true;
    binding = {value, true};
    return false;
}
void invalidate_bindings() {
    bool enabled = binding_cache.enabled;
    binding_cache = {};
    binding_cache.enabled = enabled;
}
int buffer_index(GLenum target) {
    switch (target) {
    case GL_ARRAY_BUFFER:
        return 0;
    case GL_UNIFORM_BUFFER:
        return 1;
    case GL_PIXEL_PACK_BUFFER:
        return 2;
    case GL_PIXEL_UNPACK_BUFFER:
        return 3;
    default:
        return -1; // Element-array bindings belong to their VAO.
    }
}
int capability_index(GLenum cap) {
    switch (cap) {
    case GL_DEPTH_TEST:
        return 0;
    case GL_BLEND:
        return 1;
    case GL_SCISSOR_TEST:
        return 2;
    case GL_CULL_FACE:
        return 3;
    default:
        return -1;
    }
}
void APIENTRY cached_use_program(GLuint program) {
    if (!same_binding(binding_cache.program, program))
        uncached_use_program(program);
}
void APIENTRY cached_active_texture(GLenum unit) {
    if (!same_binding(binding_cache.active, unit))
        uncached_active_texture(unit);
}
void APIENTRY cached_bind_texture(GLenum target, GLuint texture) {
    unsigned unit = binding_cache.active.value - GL_TEXTURE0;
    int index = target == GL_TEXTURE_2D ? 0 : target == GL_TEXTURE_2D_MULTISAMPLE ? 1 : -1;
    if (binding_cache.active.valid && unit < binding_cache.textures.size() && index >= 0 &&
        same_binding(binding_cache.textures[unit][index], texture))
        return;
    uncached_bind_texture(target, texture);
}
void APIENTRY cached_bind_buffer(GLenum target, GLuint buffer) {
    int index = buffer_index(target);
    if (index >= 0 && same_binding(binding_cache.buffers[index], buffer))
        return;
    uncached_bind_buffer(target, buffer);
}
void APIENTRY cached_bind_buffer_base(GLenum target, GLuint index, GLuint buffer) {
    uncached_bind_buffer_base(target, index, buffer);
    int slot = buffer_index(target);
    if (binding_cache.enabled && slot >= 0)
        binding_cache.buffers[slot] = {buffer, true};
}
void APIENTRY cached_bind_buffer_range(GLenum target, GLuint index, GLuint buffer, GLintptr offset,
                                       GLsizeiptr size) {
    uncached_bind_buffer_range(target, index, buffer, offset, size);
    int slot = buffer_index(target);
    if (binding_cache.enabled && slot >= 0)
        binding_cache.buffers[slot] = {buffer, true};
}
void APIENTRY cached_bind_vao(GLuint vao) {
    if (!same_binding(binding_cache.vao, vao))
        uncached_bind_vao(vao);
}
void APIENTRY cached_bind_fbo(GLenum target, GLuint fbo) {
    if (target == GL_FRAMEBUFFER) {
        bool draw = same_binding(binding_cache.draw_fbo, fbo);
        bool read = same_binding(binding_cache.read_fbo, fbo);
        if (draw && read)
            return;
    } else if (target == GL_DRAW_FRAMEBUFFER) {
        if (same_binding(binding_cache.draw_fbo, fbo))
            return;
    } else if (target == GL_READ_FRAMEBUFFER) {
        if (same_binding(binding_cache.read_fbo, fbo))
            return;
    }
    uncached_bind_fbo(target, fbo);
}
void APIENTRY cached_enable(GLenum cap) {
    int index = capability_index(cap);
    if (index >= 0 && same_binding(binding_cache.capabilities[index], GL_TRUE))
        return;
    uncached_enable(cap);
}
void APIENTRY cached_disable(GLenum cap) {
    int index = capability_index(cap);
    if (index >= 0 && same_binding(binding_cache.capabilities[index], GL_FALSE))
        return;
    uncached_disable(cap);
}
void APIENTRY cached_bind_image(GLuint unit, GLuint texture, GLint level, GLboolean layered,
                                GLint layer, GLenum access, GLenum format) {
    if (binding_cache.enabled && unit < binding_cache.images.size()) {
        auto &image = binding_cache.images[unit];
        const std::array<GLuint, 6> values = {texture,       GLuint(level), layered,
                                              GLuint(layer), access,        format};
        if (image.valid && image.values == values)
            return;
        image = {values, true};
    }
    uncached_bind_image(unit, texture, level, layered, layer, access, format);
}
#define CACHE_DELETE(name, raw)                                                                    \
    void APIENTRY name(GLsizei count, const GLuint *objects) {                                     \
        raw(count, objects);                                                                       \
        invalidate_bindings();                                                                     \
    }
CACHE_DELETE(cached_delete_textures, uncached_delete_textures)
CACHE_DELETE(cached_delete_buffers, uncached_delete_buffers)
CACHE_DELETE(cached_delete_vaos, uncached_delete_vaos)
CACHE_DELETE(cached_delete_fbos, uncached_delete_fbos)
#undef CACHE_DELETE
void APIENTRY cached_delete_program(GLuint program) {
    uncached_delete_program(program);
    invalidate_bindings();
}
void install_binding_cache() {
    binding_cache = {};
#define CACHE_BIND(gl, raw, wrapper)                                                               \
    raw = glad_##gl;                                                                               \
    glad_##gl = wrapper
    CACHE_BIND(glUseProgram, uncached_use_program, cached_use_program);
    CACHE_BIND(glActiveTexture, uncached_active_texture, cached_active_texture);
    CACHE_BIND(glBindTexture, uncached_bind_texture, cached_bind_texture);
    CACHE_BIND(glBindBuffer, uncached_bind_buffer, cached_bind_buffer);
    CACHE_BIND(glBindBufferBase, uncached_bind_buffer_base, cached_bind_buffer_base);
    CACHE_BIND(glBindBufferRange, uncached_bind_buffer_range, cached_bind_buffer_range);
    CACHE_BIND(glBindVertexArray, uncached_bind_vao, cached_bind_vao);
    CACHE_BIND(glBindFramebuffer, uncached_bind_fbo, cached_bind_fbo);
    CACHE_BIND(glEnable, uncached_enable, cached_enable);
    CACHE_BIND(glDisable, uncached_disable, cached_disable);
    CACHE_BIND(glDeleteTextures, uncached_delete_textures, cached_delete_textures);
    CACHE_BIND(glDeleteBuffers, uncached_delete_buffers, cached_delete_buffers);
    CACHE_BIND(glDeleteVertexArrays, uncached_delete_vaos, cached_delete_vaos);
    CACHE_BIND(glDeleteFramebuffers, uncached_delete_fbos, cached_delete_fbos);
    CACHE_BIND(glDeleteProgram, uncached_delete_program, cached_delete_program);
    if (glad_glBindImageTexture) {
        CACHE_BIND(glBindImageTexture, uncached_bind_image, cached_bind_image);
    }
#undef CACHE_BIND
}
} // namespace
void glrdp_begin_binding_cache() {
    binding_cache = {};
    binding_cache.enabled = true;
}
void glrdp_end_binding_cache() {
    binding_cache.enabled = false;
}
