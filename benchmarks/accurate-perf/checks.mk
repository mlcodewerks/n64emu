# Run from the repository root with a native x64 compiler.
OUT ?= benchmarks/accurate-perf/build
CPU := src/device/r4300
RSP := src/device/rcp/rsp/p_rsp
HOST := $(shell $(CXX) -dumpmachine)
ifneq (,$(findstring mingw,$(HOST)))
EXE := .exe
endif
INCLUDES := -Icustom -Isrc -Ideps -Ideps/xbyak -Ideps/libretro-common/include
CPU_SOURCES := benchmarks/accurate-perf/cpu_cache_checks.c $(CPU)/cpu_ops.c $(CPU)/cp0.c $(CPU)/cp1.c $(CPU)/fpu_native.c $(CPU)/idec.c $(CPU)/interrupt.c $(CPU)/jit_runtime.c $(CPU)/r4300_core.c $(CPU)/tlb.c
CPU_OBJECTS := $(addprefix $(OUT)/,$(CPU_SOURCES:.c=.o))
RSP_SOURCES := $(RSP)/rsp_jit.cpp $(RSP)/jit_vector.cpp $(RSP)/rsp_disasm.cpp $(RSP)/jit_allocator.cpp $(wildcard $(RSP)/rsp/*.cpp) $(wildcard $(RSP)/arch/simd/rsp/*.cpp)
RSP_HEADERS := $(wildcard $(RSP)/*.hpp) $(wildcard $(RSP)/*.inc) src/jit32/assembler.hpp
RSP_FLAGS := $(INCLUDES) -I$(RSP) -I$(RSP)/arch/simd/rsp -O2 -std=c++17 -DN64EMU_RSP_TEST_SILENT

.PHONY: all check proxy
all: $(OUT)/cpu-cache-checks$(EXE) $(OUT)/mixed-checks$(EXE) $(OUT)/gl-binding-checks$(EXE) $(OUT)/rsp-checks$(EXE) $(OUT)/vector-checks$(EXE)
check: all
	$(OUT)/cpu-cache-checks$(EXE)
	$(OUT)/mixed-checks$(EXE)
	$(OUT)/gl-binding-checks$(EXE)
	$(OUT)/rsp-checks$(EXE)
	$(OUT)/vector-checks$(EXE)

$(OUT):
	mkdir -p $@
$(OUT)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -O3 -g -std=gnu99 -ffunction-sections -fdata-sections -fno-strict-aliasing -DHAVE_N64EMUJIT -MMD -MP -c $< -o $@
$(OUT)/jit.o: $(CPU)/jit/n64emujit_x64.cpp $(wildcard $(CPU)/jit/x64/*.inc) $(wildcard $(CPU)/jit/*.inc) | $(OUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -O3 -g -std=c++17 -ffunction-sections -fdata-sections -MMD -MP -c $< -o $@
$(OUT)/cpu-cache-checks$(EXE): $(CPU_OBJECTS) $(OUT)/jit.o
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/mixed-checks$(EXE): benchmarks/accurate-perf/rsp_mixed_checks.cpp $(RSP_SOURCES) $(RSP)/rsp_interpreter.cpp $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) $< $(RSP_SOURCES) $(RSP)/rsp_interpreter.cpp -o $@
$(OUT)/rsp-checks$(EXE): $(RSP)/jit_tests.cpp $(RSP_SOURCES) $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) $< $(RSP_SOURCES) -o $@
$(OUT)/vector-checks$(EXE): $(RSP)/vector_jit_tests.cpp $(RSP_SOURCES) $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) $< $(RSP_SOURCES) -o $@
$(OUT)/gl-binding-checks$(EXE): benchmarks/accurate-perf/gl_binding_checks.cpp src/device/rcp/rdp/glrdp/gl_state_cache.inc src/device/rcp/rdp/glrdp/gl_loader.cpp src/device/rcp/rdp/glrdp/gl_loader.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -O2 -std=c++17 $< -o $@
proxy: benchmarks/accurate-perf/perf_proxy.dll
benchmarks/accurate-perf/perf_proxy.dll: benchmarks/accurate-perf/perf_proxy.c
	$(CC) -O2 -shared -Ideps/libretro-common/include -Isrc/device/rcp/rdp/p_rdp/vulkan-headers/include $< -o $@

-include $(CPU_OBJECTS:.o=.d) $(OUT)/jit.d
