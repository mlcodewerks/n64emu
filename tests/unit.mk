# ROM-free component suite. Invoke from repository root: make -f tests/unit.mk check-unit
OUT ?= tests/.build
PYTHON ?= python
TEST_JOBS ?= 1
override CFLAGS += -UNDEBUG
override CXXFLAGS += -UNDEBUG
include benchmarks/accurate-perf/checks.mk
.DEFAULT_GOAL := check-unit
UNIT_CPU_OBJECTS := $(filter-out $(OUT)/benchmarks/accurate-perf/cpu_cache_checks.o,$(CPU_OBJECTS)) $(CPU_JIT_OBJECTS)
UNIT_EXES := $(OUT)/cpu-operations$(EXE) $(OUT)/interrupts$(EXE) $(OUT)/off-poll$(EXE) $(OUT)/storage-devices$(EXE) $(OUT)/controllers$(EXE) $(OUT)/gb-clock$(EXE)
UNIT_EXES += $(OUT)/timing$(EXE)
UNIT_EXES += $(OUT)/memory-mapping$(EXE) $(OUT)/pif-channels$(EXE) $(OUT)/disk-geometry$(EXE)
UNIT_EXES += $(OUT)/rcp-devices$(EXE)
UNIT_EXES += $(OUT)/rdp-registers$(EXE)
UNIT_EXES += $(OUT)/timing-policy$(EXE) $(OUT)/usf-timing-policy$(EXE)
$(OUT)/timing-policy$(EXE): tests/timing_policy.c src/device/r4300/memory_timing.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 $< -o $@
$(OUT)/usf-timing-policy$(EXE): tests/timing_policy.c src/device/r4300/memory_timing.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -DN64EMU_USF -O2 -std=gnu99 $< -o $@
$(OUT)/rdp-registers$(EXE): tests/rdp_registers.c tests/support.c src/device/rcp/rdp/rdp_core.c src/device/rcp/rdp/rdp_hle_fifo.c $(wildcard src/device/rcp/rdp/*.h) tests/support.h tests/headless.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 -flto -fwhole-program $(filter %.c,$^) -lm -o $@
UNIT_DEVICE_HEADERS := $(wildcard src/device/*.h) src/device/rcp/rcp.h $(wildcard src/device/r4300/*.h) $(wildcard src/core/*.h)
$(OUT)/rdp-registers$(EXE) $(OUT)/rcp-devices$(EXE) $(OUT)/memory-mapping$(EXE) $(OUT)/pif-channels$(EXE) $(OUT)/disk-geometry$(EXE) $(OUT)/storage-devices$(EXE): $(UNIT_DEVICE_HEADERS)
$(OUT)/rcp-devices$(EXE): tests/rcp_devices.c tests/support.c src/device/rcp/rcp.c tests/support.h tests/headless.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 -flto -fwhole-program $(filter %.c,$^) -lm -o $@
$(OUT)/memory-mapping$(EXE): tests/memory_mapping.c tests/support.c src/device/memory.c tests/support.h tests/headless.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 -flto -fwhole-program $(filter %.c,$^) -o $@
$(OUT)/pif-channels$(EXE): tests/pif_channels.c tests/support.c src/device/pif.c tests/support.h tests/headless.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 -flto -fwhole-program $(filter %.c,$^) -o $@
$(OUT)/disk-geometry$(EXE): tests/disk_geometry.c tests/support.c src/device/disk.c src/device/dd_controller.c tests/support.h tests/headless.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 -flto -fwhole-program $(filter %.c,$^) -o $@
$(OUT)/timing$(EXE): $(OUT)/tests/timing.o $(UNIT_CPU_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/cpu-operations$(EXE): $(OUT)/tests/cpu_operations.o $(UNIT_CPU_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/interrupts$(EXE): $(OUT)/tests/interrupts.o $(UNIT_CPU_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/off-poll$(EXE): $(OUT)/benchmarks/fast-timing-study/off_poll_checks.o $(UNIT_CPU_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/storage-devices$(EXE): tests/storage_devices.c tests/support.c src/device/cart.c tests/headless.h tests/support.h | $(OUT)
	$(CC) $(CFLAGS) $(INCLUDES) -O2 -std=gnu99 -flto -fwhole-program $(filter %.c,$^) -lm -o $@
$(OUT)/controllers$(EXE): $(OUT)/tests/controllers.o $(OUT)/tests/support.o $(OUT)/src/device/controllers.o $(OUT)/src/device/gb.o
	$(CC) $^ -Wl,--gc-sections -lm -o $@
$(OUT)/gb-clock$(EXE): $(OUT)/tests/gb_clock.o $(OUT)/tests/support.o $(OUT)/src/device/gb.o
	$(CC) $^ -Wl,--gc-sections -lm -o $@
.PHONY: check-unit check-new
check-new: $(UNIT_EXES)
	$(PYTHON) tests/run.py --out $(OUT) --jobs $(TEST_JOBS) $(UNIT_EXES)
BASE_NAMES := jit-emit-checks boot-timing-checks rdram-checks cart-dongle-checks rom-database-checks jit-memory-emit-checks jit-source-checks resume-guard-checks jit-arena-checks mapped-data-checks arm64-emit-checks native-hot-checks hle-timing-path-checks hle-timing-refine-checks hle-timing-policy-checks nead-load-admission-checks precise-nop-checks rsp-poll-checks cpu-cache-checks cycle-pipeline-checks worker-pool-checks mixed-checks gl-binding-checks rsp-checks vector-checks
BASE_EXES := $(addprefix $(OUT)/,$(addsuffix $(EXE),$(BASE_NAMES))) $(FPU_PREPARE_CHECK) $(MAPPED_FPU_CHECK) $(FP_BRANCH_CHECK) $(FP_MIXED_CHECK) $(FP_EXACT_CHECK) $(FP_ENVIRONMENT_CHECK) $(FP_DELAY_ZERO_CHECK) $(TRAPPING_ALU_CHECK) $(AFFINITY_CHECK)
EXTRA_NAMES := regressions step-jit step-interpreter signal-checks ai-fifo-checks gpu-parser fb-notifications rdp-portable rdp-short-span $(foreach s,1 2 4,scanlines-s$(s) fill-ownership-s$(s) copy-ownership-s$(s))
EXTRA_EXES := $(addprefix $(OUT)/extra/,$(addsuffix $(EXE),$(EXTRA_NAMES)))
.PHONY: extra-build
extra-build:
	$(MAKE) -f tests/extras.mk OUT=$(OUT)/extra all
check-unit: all $(UNIT_EXES) extra-build
	$(PYTHON) tests/check_rdp_header.py
	$(PYTHON) tests/run.py --out $(OUT) --jobs $(TEST_JOBS) $(BASE_EXES) $(UNIT_EXES) $(EXTRA_EXES)
-include $(wildcard $(OUT)/tests/*.d) $(wildcard $(OUT)/src/device/*.d)
-include $(OUT)/benchmarks/fast-timing-study/off_poll_checks.d
