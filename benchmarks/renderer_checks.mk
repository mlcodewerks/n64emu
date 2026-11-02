# Use the normal core's build flags and scale variants:
# make -f Makefile -f benchmarks/renderer_checks.mk renderer-checks
RENDERER_CHECK_OBJECTS := $(SOFTRDP_OBJECTS) \
 $(SOFTRDP_CORE)/sr_dispatch.o $(SOFTRDP_DIR)/thread_bridge.o $(SOFTRDP_DIR)/dps_model.o \
 $(SOFTRDP_DIR)/worker_pool.o src/device/rcp/rdp/performance_cores.o \
 deps/libretro-common/features/features_cpu.o \
 deps/libretro-common/rthreads/rthreads.o src/renderer_options.o
RENDERER_CHECK_EXE := benchmarks/renderer_vi_checks$(if $(filter win,$(platform)),.exe)

.PHONY: renderer-checks
renderer-checks: $(RENDERER_CHECK_EXE)
	./$(RENDERER_CHECK_EXE)

$(RENDERER_CHECK_EXE): benchmarks/renderer_vi_checks.c $(RENDERER_CHECK_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(RENDERER_CHECK_OBJECTS) -o $@ -lm $(if $(filter win,$(platform)),,-lpthread)
