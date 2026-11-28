/* Standalone frontend for the core's LLE ROM tests. */
#include "libretro.h"
#include "../../src/core/runtime.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define CORE_FUNCTIONS(X) X(init) X(deinit) X(set_environment) X(set_video_refresh) \
 X(set_audio_sample) X(set_audio_sample_batch) X(set_input_poll) X(set_input_state) \
 X(load_game) X(unload_game) X(run) X(reset) X(get_memory_data) X(get_memory_size) \
 X(serialize_size) X(serialize) X(unserialize)
struct core_api {
#define DECLARE(name) __typeof__(retro_##name) *name;
 CORE_FUNCTIONS(DECLARE)
#undef DECLARE
};
static struct core_api core;
static unsigned frames, pixel_format, frame_limit;
static int done;
static const char *timing_mode = "on";
static const char *cpu_clock = "1x";
static const char *stop_marker;
static unsigned stop_marker_count=1,stop_markers_seen;
static unsigned char *capture;
static unsigned capture_width, capture_height;
enum { SCAN_WIDTH = 96, SCAN_HEIGHT = 72 };
static unsigned char scanout[SCAN_WIDTH * SCAN_HEIGHT];
static FILE *scan_file, *audio_file, *timing_file;
static uint64_t audio_frames;
static bool capture_failed;
static void (*av_timing)(struct core_av_timing_snapshot *);

static void logger(enum retro_log_level level, const char *fmt, ...) {
 char text[4096]; va_list ap; va_start(ap, fmt); vsnprintf(text, sizeof(text), fmt, ap); va_end(ap);
 fprintf(stderr,"[core %u] %s",(unsigned)level,text);fflush(stderr);
 if(stop_marker && strstr(text,stop_marker) && ++stop_markers_seen>=stop_marker_count)done=1;
 if(strstr(text,"Done!") || strstr(text,"Finished in "))done=1;
 if(strstr(text,"Exception storm detected") || strstr(text,"PANIC"))done=2;
}
static bool environment(unsigned command, void *data) {
 switch(command) {
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log=logger;return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE: {
  struct retro_variable *v=data;const char *key=v->key;
  if(!strcmp(key,"n64emu_cycle_timing"))v->value=timing_mode;
  else if(!strcmp(key,"n64emu_cpu_overclock"))v->value=cpu_clock;
  else {v->value=NULL;return false;}return true;
 }
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data=false;return true;
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)data=0;return true;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
 case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
 case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: *(const char**)data=".";return true;
 case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data=true;return true;
 case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned*)data=RETRO_LANGUAGE_ENGLISH;return true;
 case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
 case RETRO_ENVIRONMENT_GET_FASTFORWARDING: *(bool*)data=true;return true;
 case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(unsigned*)data=3;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: pixel_format=*(unsigned*)data;return true;
 case RETRO_ENVIRONMENT_SET_VARIABLES:
 case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
 case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
 case RETRO_ENVIRONMENT_SET_GEOMETRY:
 case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
 case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
 case RETRO_ENVIRONMENT_SHUTDOWN: done=1;return true;
 default: return false;
 }
}
static void capture_video(const void *data,unsigned width,unsigned height,size_t pitch) {
 if(!data || data==RETRO_HW_FRAME_BUFFER_VALID || !width || !height)return;
 if(width!=capture_width || height!=capture_height) {
  free(capture);capture=malloc((size_t)width*height*3);capture_width=width;capture_height=height;
 }
 if(!capture)abort();
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
  unsigned r,g,b;
  if(pixel_format==RETRO_PIXEL_FORMAT_XRGB8888) {
   uint32_t value;memcpy(&value,(const unsigned char*)data+y*pitch+x*4,4);r=value>>16;g=value>>8;b=value;
  } else {
   uint16_t value;memcpy(&value,(const unsigned char*)data+y*pitch+x*2,2);
   if(pixel_format==RETRO_PIXEL_FORMAT_RGB565){r=((value>>11)&31)*255/31;g=((value>>5)&63)*255/63;b=(value&31)*255/31;}
   else {r=((value>>10)&31)*255/31;g=((value>>5)&31)*255/31;b=(value&31)*255/31;}
  }
  size_t i=((size_t)y*width+x)*3;capture[i]=r;capture[i+1]=g;capture[i+2]=b;
 }
}
static void video(const void *data,unsigned width,unsigned height,size_t pitch) {
 if (!frames || frames + 1 == frame_limit || done) capture_video(data,width,height,pitch);
 if (!scan_file || !data || data == RETRO_HW_FRAME_BUFFER_VALID || !width || !height) return;
 for (unsigned y = 0; y < SCAN_HEIGHT; ++y) {
  const uint8_t *row = (const uint8_t *)data + (y * height / SCAN_HEIGHT) * pitch;
  for (unsigned x = 0; x < SCAN_WIDTH; ++x) {
   const unsigned source = x * width / SCAN_WIDTH;
   unsigned red, green, blue;
   if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
    uint32_t color;
    memcpy(&color, row + source * 4, 4);
    red = (color >> 16) & 255;
    green = (color >> 8) & 255;
    blue = color & 255;
   } else {
    uint16_t color;
    memcpy(&color, row + source * 2, 2);
    const bool rgb565 = pixel_format == RETRO_PIXEL_FORMAT_RGB565;
    red = ((color >> (rgb565 ? 11 : 10)) & 31) * 255 / 31;
    green = ((color >> 5) & (rgb565 ? 63 : 31)) * 255 / (rgb565 ? 63 : 31);
    blue = (color & 31) * 255 / 31;
   }
   scanout[y * SCAN_WIDTH + x] = (red * 77 + green * 150 + blue * 29) / 256;
  }
 }
}
static size_t audio_batch(const int16_t *data, size_t count)
{
 if (audio_file && fwrite(data, 2 * sizeof(*data), count, audio_file) != count)
  capture_failed = true;
 audio_frames += count;
 return count;
}
static void audio(int16_t left,int16_t right)
{
 const int16_t sample[2] = {left, right};
 audio_batch(sample, 1);
}
static void poll(void){}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id){(void)port;(void)device;(void)index;(void)id;return 0;}
int main(int argc,char **argv) {
 stop_marker=getenv("N64EMU_CONFORMANCE_STOP_MARKER");
 const char *marker_count=getenv("N64EMU_CONFORMANCE_STOP_COUNT");
 if(marker_count)stop_marker_count=strtoul(marker_count,NULL,10);
 if(!stop_marker_count){fputs("STOP_COUNT must be positive\n",stderr);return 2;}
#ifdef _WIN32
 SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
 int first=1;
 if(argc>first && !strcmp(argv[first],"--timing-off")){timing_mode="off";++first;}
 if(argc>first && !strncmp(argv[first],"--clock=",8)){
  cpu_clock=argv[first++]+8;
  if(strcmp(cpu_clock,"1x") && strcmp(cpu_clock,"2x") && strcmp(cpu_clock,"4x"))return 2;
 }
 const bool capture_av = argc > first && !strcmp(argv[first], "--capture-av");
 if (capture_av) ++first;
#ifdef CONFORMANCE_STATIC_CORE
#define LOAD(name) core.name=retro_##name;
 CORE_FUNCTIONS(LOAD)
#undef LOAD
#else
 if(argc<first+3)return 2;
 HMODULE module=LoadLibraryA(argv[first++]);if(!module){fprintf(stderr,"LoadLibrary failed: %lu\n",GetLastError());return 2;}
#define LOAD(name) core.name=(__typeof__(core.name))GetProcAddress(module,"retro_" #name);if(!core.name)return 2;
 CORE_FUNCTIONS(LOAD)
#undef LOAD
 if (capture_av)
  av_timing = (void (*)(struct core_av_timing_snapshot *))GetProcAddress(module, "retro_debug_av_timing");
#endif
 if(argc<first+2){fputs("runner [--timing-off] [--clock=1x|2x|4x] [--capture-av] [CORE.dll] ROM FRAMES [capture.ppm]\n",stderr);return 2;}
 if (capture_av) {
  if (!av_timing) {
   fputs("A/V capture requires a HEADLESS_BENCHMARK=1 core DLL\n", stderr);
   return 2;
  }
  scan_file = fopen("scan.gray", "wb");
  audio_file = fopen("audio.pcm", "wb");
  timing_file = fopen("timing.csv", "w");
  if (!scan_file || !audio_file || !timing_file) return 2;
  fputs("field,audio_frames,clock,vi_boundary,rdp_deadline,dp_irq,rsp_dma,ai_completion,vi_origin,ai_divider,ai_status,ai_remaining,ai_queued\n", timing_file);
 }
 const char *path=argv[first++];unsigned limit=strtoul(argv[first++],NULL,10);frame_limit=limit;
 FILE *file=fopen(path,"rb");if(!file){perror(path);return 2;}fseek(file,0,SEEK_END);long length=ftell(file);rewind(file);
 void *rom=malloc(length);if(!rom || fread(rom,1,length,file)!=(size_t)length)return 2;fclose(file);
 core.set_environment(environment);core.set_video_refresh(video);core.set_audio_sample(audio);core.set_audio_sample_batch(audio_batch);core.set_input_poll(poll);core.set_input_state(input);core.init();
 struct retro_game_info game={path,rom,(size_t)length,NULL};if(!core.load_game(&game)){fputs("load_game failed\n",stderr);return 2;}
 const char *checkpoint=getenv("N64EMU_CONFORMANCE_ROUNDTRIP_FRAME");
 unsigned checkpoint_frame=checkpoint?strtoul(checkpoint,NULL,10):0;
 const char *reset=getenv("N64EMU_CONFORMANCE_RESET_FRAME");
 unsigned reset_frame=reset?strtoul(reset,NULL,10):0;
 while(frames<limit && !done){
  core.run();++frames;
  if (capture_av) {
   struct core_av_timing_snapshot timing;
   av_timing(&timing);
   if (fwrite(scanout, 1, sizeof(scanout), scan_file) != sizeof(scanout) ||
       fprintf(timing_file,"%u,%" PRIu64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%u,%u,%u,%u,%u\n",
               frames,audio_frames,timing.clock,timing.vi_boundary,timing.rdp_deadline,timing.dp_irq,
               timing.rsp_dma,timing.ai_completion,timing.vi_origin,timing.ai_divider,timing.ai_status,
               timing.ai_remaining,timing.ai_queued) < 0)
    capture_failed = true;
   if (capture_failed) break;
  }
  if(frames==reset_frame){
   core.reset();fprintf(stderr,"CONFORMANCE soft reset frame=%u\n",frames);
  }
  if(frames==checkpoint_frame){
   size_t size=core.serialize_size();void *state=malloc(size);
   if(!state || !core.serialize(state,size) || !core.unserialize(state,size)){
    fputs("CONFORMANCE savestate roundtrip failed\n",stderr);free(state);return 3;
   }
   free(state);fprintf(stderr,"CONFORMANCE savestate roundtrip frame=%u bytes=%zu\n",frames,size);
  }
  if(frames%120==0){fprintf(stderr,"RUN frame=%u\n",frames);fflush(stderr);}
 }
 if(argc>first && capture){file=fopen(argv[first],"wb");if(file){fprintf(file,"P6\n%u %u\n255\n",capture_width,capture_height);fwrite(capture,3,(size_t)capture_width*capture_height,file);fclose(file);}}
 printf("CONFORMANCE frames=%u done=%d video=%ux%u\n",frames,done,capture_width,capture_height);fflush(stdout);
 core.unload_game();core.deinit();free(capture);free(rom);
 if (capture_av) {
  capture_failed |= fclose(scan_file) != 0;
  capture_failed |= fclose(audio_file) != 0;
  capture_failed |= fclose(timing_file) != 0;
 }
#if defined(_WIN32) && !defined(CONFORMANCE_STATIC_CORE)
 FreeLibrary(module);
#endif
 return capture_failed ? 3 : 0;
}
