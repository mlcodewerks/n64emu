OUT ?= benchmarks/conformance/out/rsp
ifneq (,$(findstring mingw,$(shell $(CXX) -dumpmachine)))
EXE := .exe
endif
RSP := src/device/rcp/rsp/p_rsp
SOURCES := $(RSP)/rsp_jit.cpp $(RSP)/jit_vector.cpp $(RSP)/rsp_disasm.cpp $(RSP)/jit_allocator.cpp $(wildcard $(RSP)/rsp/*.cpp) $(wildcard $(RSP)/arch/simd/rsp/*.cpp)
ifneq (,$(findstring aarch64,$(shell $(CXX) -dumpmachine)))
SOURCES += deps/xbyak_aarch64/src/xbyak_aarch64_impl.cpp
endif
FLAGS := -Icustom -Isrc -Ideps -Ideps/xbyak -Ideps/xbyak_aarch64 -Ideps/xbyak_aarch64/xbyak_aarch64 -Ideps/libretro-common/include -I$(RSP) -I$(RSP)/arch/simd/rsp -O2 -std=c++17 -ffunction-sections -fdata-sections -DN64EMU_RSP_TEST_SILENT
$(OUT)/regressions$(EXE): benchmarks/conformance/rsp_regressions.cpp $(RSP)/jit_tests.cpp $(SOURCES) $(wildcard $(RSP)/*.hpp) $(wildcard $(RSP)/*.inc)
	mkdir -p $(OUT)
	$(CXX) $(FLAGS) $< $(SOURCES) -Wl,--gc-sections $(LDFLAGS) -o $@
