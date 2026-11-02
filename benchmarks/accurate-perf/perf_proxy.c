#define WIN32_LEAN_AND_MEAN
#include "libretro.h"
#define VK_NO_PROTOTYPES
#include "libretro_vulkan.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
static HMODULE module;
static retro_environment_t environment;
static const struct retro_hw_render_interface_vulkan *vulkan;
static retro_audio_sample_batch_t audio_cb;
static unsigned frames;
static uint64_t ticks, warm_ticks, audio_frames,
    audio_hash = 1469598103934665603ULL;
static FARPROC symbol(const char *name) {
  if (!module)
    module = LoadLibraryA(getenv("N64EMU_TEST_CORE"));
  FARPROC p = module ? GetProcAddress(module, name) : NULL;
  if (!p) {
    fprintf(stderr, "Missing %s error=%lu\n", name, GetLastError());
    abort();
  }
  return p;
}
static uint64_t hash(uint64_t h, const void *ptr, size_t n) {
  const uint8_t *p = ptr;
  while (n--) {
    h ^= *p++;
    h *= 1099511628211ULL;
  }
  return h;
}
static size_t audio_shim(const int16_t *data, size_t n) {
  audio_hash = hash(audio_hash, data, n * 4);
  audio_frames += n;
  return audio_cb(data, n);
}
static int16_t input_shim(unsigned a, unsigned b, unsigned c, unsigned d) {
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  return 0;
}
static bool env_shim(unsigned cmd, void *data) {
  bool ok = environment(cmd, data);
  if (ok && cmd == RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE) {
    const struct retro_hw_render_interface *iface =
        *(const struct retro_hw_render_interface **)data;
    if (iface && iface->interface_type == RETRO_HW_RENDER_INTERFACE_VULKAN)
      vulkan = (const struct retro_hw_render_interface_vulkan *)iface;
  }
  return ok;
}
static void synchronized_checkpoint(void) {
  if (!getenv("N64EMU_PERF_SYNC_CHECKPOINTS"))
    return;
  // scanout() drains the RDP command ring before retro_run returns. The GPU
  // can still be writing host-coherent RDRAM; serialization alone does not
  // fence that Vulkan work. Use the frontend-owned device and its queue lock.
  if (vulkan) {
    PFN_vkDeviceWaitIdle wait = (PFN_vkDeviceWaitIdle)vulkan->get_device_proc_addr(
        vulkan->device, "vkDeviceWaitIdle");
    if (!wait)
      abort();
    if (vulkan->lock_queue && vulkan->unlock_queue)
      vulkan->lock_queue(vulkan->handle);
    VkResult result = wait(vulkan->device);
    if (vulkan->lock_queue && vulkan->unlock_queue)
      vulkan->unlock_queue(vulkan->handle);
    if (result != VK_SUCCESS)
      abort();
  }
  size_t n = ((size_t (*)(void))symbol("retro_serialize_size"))();
  void *p = malloc(n);
  if (!p || !((bool (*)(void *, size_t))symbol("retro_serialize"))(p, n)) {
    fputs("Synchronized checkpoint failed\n", stderr);
    abort();
  }
  fprintf(stderr, "TRACE_STATE frame=%u state=%016llx\n", frames,
          (unsigned long long)hash(1469598103934665603ULL, p, n));
  free(p);
}
static void save_snapshot(void) {
  const char *path = getenv("N64EMU_PERF_STATE");
  if (path) {
    size_t n = ((size_t (*)(void))symbol("retro_serialize_size"))();
    void *p = malloc(n);
    if (p && ((bool (*)(void *, size_t))symbol("retro_serialize"))(p, n)) {
      FILE *f = fopen(path, "wb");
      if (f) {
        fwrite(p, 1, n, f);
        fclose(f);
        fprintf(stderr, "STATE bytes=%llu\n", (unsigned long long)n);
      }
    }
    free(p);
  }
}
#define API __declspec(dllexport)
#define FORWARD_VOID(name, decl, args)                                         \
  API void name decl { ((void(*) decl)symbol(#name)) args; }
#define FORWARD_RET(type, name, decl, args)                                    \
  API type name decl { return ((type(*) decl)symbol(#name))args; }
API void retro_set_environment(retro_environment_t env) {
  environment = env;
  ((void (*)(retro_environment_t))symbol("retro_set_environment"))(env_shim);
}
API void retro_run(void) {
  LARGE_INTEGER a, b;
  QueryPerformanceCounter(&a);
  ((void (*)(void))symbol("retro_run"))();
  QueryPerformanceCounter(&b);
  ++frames;
  ticks += b.QuadPart - a.QuadPart;
  if (frames > 480)
    warm_ticks += b.QuadPart - a.QuadPart;
  if (frames % 120 == 0) {
    synchronized_checkpoint();
    void *ram = ((void *(*)(unsigned))symbol("retro_get_memory_data"))(
        RETRO_MEMORY_SYSTEM_RAM);
    size_t n = ((size_t (*)(unsigned))symbol("retro_get_memory_size"))(
        RETRO_MEMORY_SYSTEM_RAM);
    fprintf(stderr, "TRACE frame=%u ram=%016llx audio=%016llx samples=%llu\n",
            frames, (unsigned long long)hash(1469598103934665603ULL, ram, n),
            (unsigned long long)audio_hash, (unsigned long long)audio_frames);
    fflush(stderr);
  }
  const char *last = getenv("N64EMU_PERF_FRAMES");
  if (last && frames == strtoul(last, NULL, 10))
    save_snapshot();
}
API void retro_deinit(void) {
  ((void (*)(void))symbol("retro_deinit"))();
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  fprintf(
      stderr,
      "PERF frames=%u core_ms=%.6f warm_ms=%.6f audio=%016llx samples=%llu\n",
      frames, ticks * 1000.0 / frequency.QuadPart,
      warm_ticks * 1000.0 / frequency.QuadPart, (unsigned long long)audio_hash,
      (unsigned long long)audio_frames);
  fflush(stderr);
}
FORWARD_VOID(retro_init, (void), ())
FORWARD_RET(unsigned, retro_api_version, (void), ())
FORWARD_VOID(retro_set_video_refresh, (retro_video_refresh_t cb), (cb))
FORWARD_VOID(retro_set_audio_sample, (retro_audio_sample_t cb), (cb))
API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  audio_cb = cb;
  ((void (*)(retro_audio_sample_batch_t))symbol(
      "retro_set_audio_sample_batch"))(audio_shim);
}
FORWARD_VOID(retro_set_input_poll, (retro_input_poll_t cb), (cb))
API void retro_set_input_state(retro_input_state_t cb) {
  (void)cb;
  ((void (*)(retro_input_state_t))symbol("retro_set_input_state"))(input_shim);
}
FORWARD_VOID(retro_get_system_info, (struct retro_system_info * info), (info))
FORWARD_VOID(retro_get_system_av_info, (struct retro_system_av_info * info),
             (info))
FORWARD_VOID(retro_set_controller_port_device, (unsigned p, unsigned d), (p, d))
FORWARD_VOID(retro_reset, (void), ())
FORWARD_RET(size_t, retro_serialize_size, (void), ())
FORWARD_RET(bool, retro_serialize, (void *d, size_t n), (d, n))
FORWARD_RET(bool, retro_unserialize, (const void *d, size_t n), (d, n))
FORWARD_VOID(retro_cheat_reset, (void), ())
FORWARD_VOID(retro_cheat_set, (unsigned i, bool e, const char *c), (i, e, c))
FORWARD_RET(bool, retro_load_game, (const struct retro_game_info *g), (g))
FORWARD_RET(bool, retro_load_game_special,
            (unsigned t, const struct retro_game_info *g, size_t n), (t, g, n))
API void retro_unload_game(void) {
  save_snapshot();
  ((void (*)(void))symbol("retro_unload_game"))();
}
FORWARD_RET(unsigned, retro_get_region, (void), ())
FORWARD_RET(void *, retro_get_memory_data, (unsigned i), (i))
FORWARD_RET(size_t, retro_get_memory_size, (unsigned i), (i))
