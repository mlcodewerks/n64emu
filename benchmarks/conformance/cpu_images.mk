OUT ?= benchmarks/conformance/out/cpu-images
CPU := src/device/r4300
HOST := $(shell $(CXX) -dumpmachine)
ifneq (,$(findstring mingw,$(HOST)))
EXE := .exe
endif
ifneq (,$(findstring aarch64,$(HOST)))
EMITTER := $(CPU)/jit/n64emujit_arm64.cpp
EMITTER_OBJECTS := $(OUT)/jit-impl.o
else ifneq (,$(findstring arm,$(HOST)))
EMITTER := $(CPU)/jit/n64emujit_arm32.cpp
else ifneq (,$(findstring i686,$(HOST)))
EMITTER := $(CPU)/jit/n64emujit_x86.cpp
else
EMITTER := $(CPU)/jit/n64emujit_x64.cpp
endif
INCLUDES := -Icustom -Isrc -Ideps -Ideps/xbyak -Ideps/xbyak_aarch64 -Ideps/xbyak_aarch64/xbyak_aarch64 -Ideps/libretro-common/include
SOURCES := benchmarks/conformance/thar0_runner.c $(addprefix $(CPU)/,cpu_ops.c cp0.c cp1.c fpu_native.c idec.c interrupt.c jit_runtime.c r4300_core.c tlb.c)
OBJECTS := $(addprefix $(OUT)/,$(SOURCES:.c=.o))
all: $(OUT)/thar0-runner$(EXE)
$(OUT)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(INCLUDES) -O3 -g -std=gnu99 -ffunction-sections -fdata-sections -fno-strict-aliasing -DHAVE_N64EMUJIT -MMD -MP -c $< -o $@
$(OUT)/jit.o: $(EMITTER) $(wildcard $(CPU)/jit/*/*.inc) | $(OUT)
	$(CXX) $(INCLUDES) -O3 -g -std=c++17 -ffunction-sections -fdata-sections -MMD -MP -c $< -o $@
$(OUT)/jit-impl.o: deps/xbyak_aarch64/src/xbyak_aarch64_impl.cpp | $(OUT)
	$(CXX) $(INCLUDES) -O3 -std=c++17 -ffunction-sections -fdata-sections -c $< -o $@
$(OUT):
	mkdir -p $@
$(OUT)/thar0-runner$(EXE): $(OBJECTS) $(OUT)/jit.o $(EMITTER_OBJECTS)
	$(CXX) $^ -Wl,--gc-sections $(LDFLAGS) -lm -o $@
-include $(OBJECTS:.o=.d) $(OUT)/jit.d
