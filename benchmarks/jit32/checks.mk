# Run from the repository root with the matching 32-bit compiler.
OUT ?= build/jit32
CXXFLAGS ?= -O2
CPPFLAGS += -Isrc -Icustom -Ideps/xbyak -Isrc/device/rcp/rsp/p_rsp/arch/simd/rsp
HOST := $(shell $(CXX) -dumpmachine)
ifneq (,$(findstring arm,$(HOST)))
CPU_EMITTER := src/device/r4300/jit/n64emujit_arm32.cpp
else
CPU_EMITTER := src/device/r4300/jit/n64emujit_x86.cpp
endif
ifneq (,$(findstring mingw,$(HOST)))
EXE := .exe
endif
RSP := src/device/rcp/rsp/p_rsp
RSP_SOURCES := $(RSP)/rsp_jit.cpp $(RSP)/jit_vector.cpp $(RSP)/rsp_disasm.cpp $(RSP)/jit_allocator.cpp $(wildcard $(RSP)/rsp/*.cpp) $(wildcard $(RSP)/arch/simd/rsp/*.cpp)
RSP_HEADERS := $(wildcard $(RSP)/*.hpp) $(wildcard $(RSP)/*.inc) src/jit32/assembler.hpp
.PHONY: all check
all: $(OUT)/cpu-checks$(EXE) $(OUT)/rsp-checks$(EXE) $(OUT)/vector-checks$(EXE)
$(OUT):
	mkdir -p $@
$(OUT)/cpu-checks$(EXE): benchmarks/jit32/cpu_checks.cpp $(CPU_EMITTER) $(wildcard src/device/r4300/jit/pair32/*.inc) src/jit32/assembler.hpp | $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 $< $(CPU_EMITTER) -o $@
$(OUT)/emitter-checks$(EXE): $(RSP)/jit_tests.cpp $(RSP)/jit_allocator.cpp $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -DN64EMU_RSP_EMITTER_ONLY -ffunction-sections -fdata-sections $< $(RSP)/jit_allocator.cpp -Wl,--gc-sections -o $@
$(OUT)/rsp-checks$(EXE): $(RSP)/jit_tests.cpp $(RSP_SOURCES) $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -DN64EMU_RSP_TEST_SILENT $< $(RSP_SOURCES) -o $@
$(OUT)/vector-checks$(EXE): $(RSP)/vector_jit_tests.cpp $(RSP_SOURCES) $(RSP_HEADERS) | $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -DN64EMU_RSP_TEST_SILENT $< $(RSP_SOURCES) -o $@
check: all
	$(OUT)/cpu-checks$(EXE)
	$(OUT)/rsp-checks$(EXE)
	$(OUT)/vector-checks$(EXE)
