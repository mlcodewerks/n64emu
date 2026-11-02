#include "../../src/device/rcp/rdp/glrdp/gl_loader.cpp"
#include <cstdlib>

static unsigned calls;
static void check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}
static void APIENTRY one(GLuint) { ++calls; }
static void APIENTRY two(GLenum, GLuint) { ++calls; }
static void APIENTRY base(GLenum, GLuint, GLuint) { ++calls; }
static void APIENTRY range(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr) {
  ++calls;
}
static void APIENTRY remove(GLsizei, const GLuint *) { ++calls; }
static void APIENTRY image(GLuint, GLuint, GLint, GLboolean, GLint, GLenum,
                           GLenum) {
  ++calls;
}

int main() {
  glad_glUseProgram = one;
  glad_glActiveTexture = one;
  glad_glBindTexture = two;
  glad_glBindBuffer = two;
  glad_glBindBufferBase = base;
  glad_glBindBufferRange = range;
  glad_glBindVertexArray = one;
  glad_glBindFramebuffer = two;
  glad_glEnable = one;
  glad_glDisable = one;
  glad_glDeleteTextures = remove;
  glad_glDeleteBuffers = remove;
  glad_glDeleteVertexArrays = remove;
  glad_glDeleteFramebuffers = remove;
  glad_glDeleteProgram = one;
  glad_glBindImageTexture = image;
  install_binding_cache();

  glUseProgram(7);
  glUseProgram(7);
  check(calls == 2, "calls outside a guard must always reach GL");
  glrdp_begin_binding_cache();
  calls = 0;
  glUseProgram(7);
  glUseProgram(7);
  check(calls == 1, "program binding must be deduplicated inside a guard");
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 3);
  glBindTexture(GL_TEXTURE_2D, 3);
  check(calls == 3, "identical texture bindings");
  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 3);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 3);
  check(calls == 6, "texture unit and target must be independent");
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 3);
  check(calls == 7, "switching units must retain each binding");

  calls = 0;
  glBindBuffer(GL_UNIFORM_BUFFER, 4);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, 5);
  glBindBuffer(GL_UNIFORM_BUFFER, 4);
  check(calls == 3, "indexed binding also changes the generic buffer");
  glBindBufferRange(GL_UNIFORM_BUFFER, 0, 6, 0, 256);
  glBindBuffer(GL_UNIFORM_BUFFER, 6);
  check(calls == 4, "range binding updates the generic buffer cache");
  glBindBufferRange(GL_UNIFORM_BUFFER, 0, 6, 256, 256);
  check(calls == 5, "range/offset changes must always reach GL");
  glBindVertexArray(1);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 9);
  glBindVertexArray(2);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 9);
  check(calls == 9, "element buffers are VAO state, not generic state");

  calls = 0;
  glBindFramebuffer(GL_FRAMEBUFFER, 3);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 3);
  check(calls == 2, "read/draw framebuffer bindings are independent");
  glBindFramebuffer(GL_FRAMEBUFFER, 3);
  check(calls == 3, "combined framebuffer binding must update both targets");
  glEnable(GL_BLEND);
  glEnable(GL_BLEND);
  glDisable(GL_BLEND);
  glDisable(GL_BLEND);
  check(calls == 5, "enable and disable share one capability state");

  calls = 0;
  glBindImageTexture(0, 5, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
  glBindImageTexture(0, 5, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
  glBindImageTexture(0, 5, 1, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
  glBindImageTexture(0, 5, 1, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
  glBindImageTexture(0, 5, 1, GL_TRUE, 1, GL_READ_WRITE, GL_RGBA8);
  glBindImageTexture(0, 5, 1, GL_TRUE, 1, GL_READ_ONLY, GL_RGBA8);
  glBindImageTexture(0, 5, 1, GL_TRUE, 1, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, 5, 1, GL_TRUE, 1, GL_READ_ONLY, GL_R32UI);
  check(calls == 7, "all image binding fields must participate");

  const GLuint id = 3;
  glUseProgram(7);
  calls = 0;
  glDeleteTextures(1, &id);
  glUseProgram(7);
  glDeleteBuffers(1, &id);
  glUseProgram(7);
  glDeleteFramebuffers(1, &id);
  glUseProgram(7);
  glDeleteVertexArrays(1, &id);
  glUseProgram(7);
  glDeleteProgram(id);
  glUseProgram(7);
  check(calls == 10,
        "deletion invalidates cached names and automatic unbindings");
  glrdp_end_binding_cache();
  calls = 0;
  glUseProgram(7);
  glUseProgram(7);
  glrdp_begin_binding_cache();
  glUseProgram(7);
  glUseProgram(7);
  check(calls == 3, "frontend/context boundaries invalidate every binding");
  glrdp_end_binding_cache();
  std::puts("GL binding cache: ownership, indexed buffers, VAOs, FBOs, images "
            "and deletion checks passed.");
}
