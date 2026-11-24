# Run from the repository root with a native x64 compiler.
OUT ?= benchmarks/accurate-perf/build
.DEFAULT_GOAL := all
CPU := src/device/r4300
RSP := src/device/rcp/rsp/p_rsp
HOST := $(shell $(CXX) -dumpmachine)
ifneq (,$(findstring mingw,$(HOST)))
EXE := .exe
AFFINITY_CHECK := $(OUT)/affinity-scope-checks$(EXE)
endif
ifneq (,$(findstring x86_64,$(HOST)))
FPU_PREPARE_CHECK := $(OUT)/fpu-prepare-checks$(EXE)
MAPPED_FPU_CHECK := $(OUT)/mapped-fpu-checks$(EXE)
TRAPPING_ALU_CHECK := $(OUT)/trapping-alu-checks$(EXE)
FP_BRANCH_CHECK := $(OUT)/fp-branch-checks$(EXE)
FP_MIXED_CHECK := $(OUT)/fp-mixed-checks$(EXE)
FP_EXACT_CHECK := $(OUT)/fp-exact-checks$(EXE)
FP_ENVIRONMENT_CHECK := $(OUT)/fp-environment-checks$(EXE)
FP_DELAY_ZERO_CHECK := $(OUT)/fp-delay-zero-checks$(EXE)
endif
INCLUDES := -Icustom -Isrc -Ideps -Ideps/xbyak -Ideps/xbyak_aarch64 -Ideps/xbyak_aarch64/xbyak_aarch64 -Ideps/libretro-common/include
CPU_EMITTER ?= $(CPU)/jit/n64emujit_x64.cpp
CPU_JIT_OBJECTS := $(OUT)/jit.o
ifneq (,$(findstring aarch64,$(HOST)))
CPU_JIT_OBJECTS += $(OUT)/xbyak-aarch64.o
endif
$(OUT)/xbyak-aarch64.o: deps/xbyak_aarch64/src/xbyak_aarch64_impl.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -O2 -std=c++17 -ffunction-sections -fdata-sections -c $< -o $@
CPU_SOURCES := benchmarks/accurate-perf/cpu_cache_checks.c $(CPU)/cpu_ops.c $(CPU)/cp0.c $(CPU)/cp1.c $(CPU)/fpu_native.c $(CPU)/idec.c $(CPU)/interrupt.c $(CPU)/jit_runtime.c $(CPU)/r4300_core.c $(CPU)/tlb.c
CPU_OBJECTS := $(addprefix $(OUT)/,$(CPU_SOURCES:.c=.o))
RSP_SOURCES := $(RSP)/rsp_jit.cpp $(RSP)/jit_vector.cpp $(RSP)/rsp_disasm.cpp $(RSP)/jit_allocator.cpp $(wildcard $(RSP)/rsp/*.cpp) $(wildcard $(RSP)/arch/simd/rsp/*.cpp)
RSP_HEADERS := $(wildcard $(RSP)/*.hpp) $(wildcard $(RSP)/*.inc) src/jit32/assembler.hpp
RSP_FLAGS := $(INCLUDES) -I$(RSP) -I$(RSP)/arch/simd/rsp -O2 -std=c++17 -DN64EMU_RSP_TEST_SILENT

$(OUT)/rsp-simd-checks$(EXE): benchmarks/rsp_simd_checks.cpp $(RSP_SOURCES) $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) -UNDEBUG -ffunction-sections -fdata-sections $< $(RSP_SOURCES) -Wl,--gc-sections -o $@

.PHONY: all check proxy
all: $(OUT)/jit-emit-checks$(EXE)
all: $(OUT)/boot-timing-checks$(EXE)
all: $(OUT)/rdram-checks$(EXE)
all: $(OUT)/cart-dongle-checks$(EXE)
all: $(OUT)/rom-database-checks$(EXE)
all: $(OUT)/jit-memory-emit-checks$(EXE) $(OUT)/jit-source-checks$(EXE)
all: $(OUT)/resume-guard-checks$(EXE)
all: $(OUT)/jit-arena-checks$(EXE)
all: $(OUT)/mapped-data-checks$(EXE)
all: $(OUT)/arm64-emit-checks$(EXE)
all: $(OUT)/native-hot-checks$(EXE)
all: $(OUT)/rsp-simd-checks$(EXE)
all: $(FPU_PREPARE_CHECK) $(MAPPED_FPU_CHECK)
all: $(FP_BRANCH_CHECK) $(FP_MIXED_CHECK) $(FP_EXACT_CHECK) $(FP_ENVIRONMENT_CHECK)
all: $(FP_DELAY_ZERO_CHECK)
all: $(TRAPPING_ALU_CHECK)
all: $(OUT)/precise-nop-checks$(EXE)
all: $(OUT)/rsp-poll-checks$(EXE)
all: $(OUT)/cpu-cache-checks$(EXE) $(OUT)/cycle-pipeline-checks$(EXE) $(OUT)/worker-pool-checks$(EXE) $(AFFINITY_CHECK) $(OUT)/mixed-checks$(EXE) $(OUT)/rsp-checks$(EXE) $(OUT)/vector-checks$(EXE)
check: all
	$(OUT)/cart-dongle-checks$(EXE)
	$(OUT)/rom-database-checks$(EXE)
	$(OUT)/boot-timing-checks$(EXE)
	$(OUT)/rdram-checks$(EXE)
	$(OUT)/jit-arena-checks$(EXE)
	$(OUT)/arm64-emit-checks$(EXE)
	$(OUT)/native-hot-checks$(EXE)
	$(OUT)/rsp-simd-checks$(EXE)
ifneq ($(TRAPPING_ALU_CHECK),)
	$(TRAPPING_ALU_CHECK)
endif
ifneq ($(FPU_PREPARE_CHECK),)
	$(FPU_PREPARE_CHECK)
	$(MAPPED_FPU_CHECK)
	$(FP_BRANCH_CHECK)
	$(FP_MIXED_CHECK)
	$(FP_EXACT_CHECK)
	$(FP_ENVIRONMENT_CHECK)
	$(FP_DELAY_ZERO_CHECK)
endif
	$(OUT)/mapped-data-checks$(EXE)
	$(OUT)/resume-guard-checks$(EXE)
	$(OUT)/jit-memory-emit-checks$(EXE)
	$(OUT)/jit-source-checks$(EXE)
	$(OUT)/jit-emit-checks$(EXE)
	$(OUT)/precise-nop-checks$(EXE)
	$(OUT)/rsp-poll-checks$(EXE)
	$(OUT)/cpu-cache-checks$(EXE)
	$(OUT)/cycle-pipeline-checks$(EXE)
	$(OUT)/worker-pool-checks$(EXE)
ifneq ($(AFFINITY_CHECK),)
	$(AFFINITY_CHECK)
endif
	$(OUT)/mixed-checks$(EXE)
	$(OUT)/rsp-checks$(EXE)
	$(OUT)/vector-checks$(EXE)

$(OUT):
	mkdir -p $@
$(OUT)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -O3 -g -std=gnu99 -ffunction-sections -fdata-sections -fno-strict-aliasing -DHAVE_N64EMUJIT -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
$(OUT)/jit.o: $(CPU_EMITTER) $(wildcard $(CPU)/jit/x64/*.inc) $(wildcard $(CPU)/jit/arm64/*.inc) $(wildcard $(CPU)/jit/pair32/*.inc) $(wildcard $(CPU)/jit/*.inc) $(wildcard $(CPU)/jit/arm64/*.h) $(wildcard $(CPU)/jit/x64/*.h) src/jit32/assembler.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DN64EMUJIT_GUARD_TEST -O3 -g -std=c++17 -ffunction-sections -fdata-sections -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
$(OUT)/cpu-cache-checks$(EXE): $(CPU_OBJECTS) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/cart-dongle-checks$(EXE): benchmarks/cart_dongle_checks.c src/device/cart.c src/device/cart.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 -flto -fwhole-program $(filter %.c,$^) -o $@
$(OUT)/rom-database-checks$(EXE): benchmarks/rom_database_checks.c src/main/rom_database.c src/main/rom_database.h src/main/rom_database_ares.h src/main/rom_database_mister.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 $(filter %.c,$^) -o $@
$(OUT)/rdram-checks$(EXE): benchmarks/rdram_checks.c src/device/rdram.c src/device/rdram.h src/device/memory.h src/device/rcp/rcp.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 $(filter %.c,$^) -o $@
$(OUT)/boot-timing-checks$(EXE): $(OUT)/benchmarks/boot_timing_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
-include $(OUT)/benchmarks/boot_timing_checks.d
$(OUT)/cycle-pipeline-checks$(EXE): $(OUT)/benchmarks/cycle_pipeline_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/native-hot-checks$(EXE): $(OUT)/benchmarks/softrdp/native_hot_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/fpu-prepare-checks$(EXE): $(OUT)/benchmarks/softrdp/fpu_prepare_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/fp-branch-checks$(EXE): $(OUT)/benchmarks/softrdp/fp_branch_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/fp-mixed-checks$(EXE): $(OUT)/benchmarks/softrdp/fp_mixed_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/fp-exact-checks$(EXE): $(OUT)/benchmarks/softrdp/fp_exact_math_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/fp-environment-checks$(EXE): $(OUT)/benchmarks/softrdp/fp_environment_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -Wl,--wrap=r4300_ops_jit_fpu_prepare -Wl,--wrap=r4300_ops_jit_fpu_prepare_prefetched -lm -o $@
$(OUT)/fp-delay-zero-checks$(EXE): $(OUT)/benchmarks/softrdp/fp_delay_zero_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
-include $(OUT)/benchmarks/softrdp/fp_branch_checks.d $(OUT)/benchmarks/softrdp/fp_mixed_checks.d $(OUT)/benchmarks/softrdp/fp_exact_math_checks.d $(OUT)/benchmarks/softrdp/fp_environment_checks.d
-include $(OUT)/benchmarks/softrdp/fp_delay_zero_checks.d
$(OUT)/mapped-fpu-checks$(EXE): $(OUT)/benchmarks/softrdp/mapped_fpu_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -Wl,--wrap=r4300_ops_jit_fpu_prepare -Wl,--wrap=r4300_ops_jit_fpu_prepare_prefetched -lm -o $@
$(OUT)/trapping-alu-checks$(EXE): $(OUT)/benchmarks/softrdp/trapping_alu_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
-include $(OUT)/benchmarks/softrdp/trapping_alu_checks.d
-include $(OUT)/benchmarks/softrdp/fpu_prepare_checks.d
-include $(OUT)/benchmarks/softrdp/mapped_fpu_checks.d
-include $(OUT)/benchmarks/softrdp/native_hot_checks.d
$(OUT)/jit-arena-checks$(EXE): $(OUT)/benchmarks/softrdp/jit_arena_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
-include $(OUT)/benchmarks/softrdp/jit_arena_checks.d
$(OUT)/resume-guard-checks$(EXE): $(OUT)/benchmarks/softrdp/resume_guard_checks.o $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/mapped-data-checks$(EXE): $(OUT)/benchmarks/softrdp/mapped_data_checks.o $(filter-out $(OUT)/$(CPU)/jit_runtime.o $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
-include $(OUT)/benchmarks/softrdp/mapped_data_checks.d
-include $(OUT)/benchmarks/softrdp/resume_guard_checks.d
$(OUT)/arm64-emit-checks$(EXE): benchmarks/softrdp/arm64_emit_checks.cpp $(CPU)/jit/arm64/xbyak_emit.h deps/xbyak_aarch64/src/xbyak_aarch64_impl.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -O2 -std=c++17 $< deps/xbyak_aarch64/src/xbyak_aarch64_impl.cpp -o $@
$(OUT)/jit-emit-checks$(EXE): benchmarks/softrdp/jit_emit_checks.cpp $(CPU)/jit/x64/xbyak_emit.h $(CPU)/jit/x64/assem_x64.h | $(OUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -O2 -std=c++17 $< -o $@
$(OUT)/jit-memory-emit-checks$(EXE): benchmarks/softrdp/jit_memory_emit_checks.cpp $(CPU)/jit/x64/xbyak_emit.h $(CPU)/jit/x64/assem_x64.h | $(OUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -O2 -std=c++17 $< -o $@
$(OUT)/jit-source-checks.o: benchmarks/softrdp/jit_source_checks.c $(CPU)/jit_runtime.c benchmarks/accurate-perf/cpu_cache_checks.c | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O3 -g -std=gnu99 -ffunction-sections -fdata-sections -fno-strict-aliasing -DHAVE_N64EMUJIT -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
$(OUT)/jit-source-checks$(EXE): $(OUT)/jit-source-checks.o $(filter-out $(OUT)/$(CPU)/jit_runtime.o $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
-include $(OUT)/jit-source-checks.d
$(OUT)/precise-nop-checks.o: benchmarks/softrdp/precise_nop_checks.c $(CPU)/jit_runtime.c benchmarks/accurate-perf/cpu_cache_checks.c | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O3 -g -std=gnu99 -ffunction-sections -fdata-sections -fno-strict-aliasing -DHAVE_N64EMUJIT -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
$(OUT)/precise-nop-checks$(EXE): $(OUT)/precise-nop-checks.o $(filter-out $(OUT)/$(CPU)/jit_runtime.o $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
-include $(OUT)/precise-nop-checks.d
$(OUT)/affinity-scope-checks$(EXE): benchmarks/affinity_scope_checks.c src/device/rcp/rdp/performance_cores.c src/device/rcp/rdp/performance_cores.h | $(OUT)
	$(CC) $(INCLUDES) -O2 -std=c11 $< -o $@
$(OUT)/worker-pool-checks$(EXE): benchmarks/worker_pool_checks.c src/device/rcp/rdp/softrdp/worker_pool.c src/device/rcp/rdp/softrdp/worker_pool.h src/device/rcp/rdp/softrdp/worker_atomic.h src/device/rcp/rdp/softrdp/worker_eventcount.c src/device/rcp/rdp/softrdp/worker_eventcount.h src/device/rcp/rdp/performance_cores.c src/device/rcp/rdp/performance_cores.h deps/libretro-common/rthreads/rthreads.c deps/libretro-common/features/features_cpu.c | $(OUT)
	$(CC) $(INCLUDES) -O2 -std=gnu11 $(filter %.c,$^) -pthread -o $@
$(OUT)/mixed-checks$(EXE): benchmarks/accurate-perf/rsp_mixed_checks.cpp $(RSP_SOURCES) $(RSP)/rsp_interpreter.cpp $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) $< $(RSP_SOURCES) $(RSP)/rsp_interpreter.cpp -o $@
$(OUT)/rsp-poll-checks$(EXE): benchmarks/softrdp/rsp_poll_checks.cpp $(RSP_SOURCES) $(RSP)/rsp_interpreter.cpp $(RSP_HEADERS) $(RSP)/rsp/timing.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) -DPARALLEL_INTEGRATION $< $(RSP_SOURCES) $(RSP)/rsp_interpreter.cpp -o $@
$(OUT)/rsp-checks$(EXE): $(RSP)/jit_tests.cpp $(RSP_SOURCES) $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) $< $(RSP_SOURCES) -o $@
$(OUT)/vector-checks$(EXE): $(RSP)/vector_jit_tests.cpp $(RSP_SOURCES) $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(RSP_FLAGS) $< $(RSP_SOURCES) -o $@
proxy: benchmarks/accurate-perf/perf_proxy.dll
benchmarks/accurate-perf/perf_proxy.dll: benchmarks/accurate-perf/perf_proxy.c
	$(CC) -O2 -shared -Ideps/libretro-common/include -Isrc/device/rcp/rdp/p_rdp/vulkan-headers/include $< -o $@

-include $(CPU_OBJECTS:.o=.d) $(OUT)/benchmarks/cycle_pipeline_checks.d $(OUT)/jit.d
