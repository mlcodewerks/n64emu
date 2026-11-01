# Run from the repository root: make -f benchmarks/portable_checks.mk
CFLAGS ?= -O2
CXXFLAGS ?= -O2
RSP := src/device/rcp/rsp/p_rsp
RSP_LEAVES := $(wildcard $(RSP)/rsp/*.cpp) $(wildcard $(RSP)/arch/simd/rsp/*.cpp)
RSP_HEADERS := $(wildcard $(RSP)/rsp/*.hpp) $(RSP)/state.hpp $(RSP)/rsp_op.hpp
HOST_TRIPLE := $(shell $(CC) -dumpmachine)
ifneq (,$(findstring mingw,$(HOST_TRIPLE)))
EXE := .exe
else
DL_LIB := -ldl
endif

.PHONY: all
all: benchmarks/rsp_interpreter_checks$(EXE) benchmarks/usf_checks$(EXE) benchmarks/libretro_portable_checks$(EXE)

benchmarks/rsp_interpreter_checks$(EXE): $(RSP)/jit_tests.cpp $(RSP)/rsp_interpreter.cpp $(RSP)/rsp_interpreter.hpp $(RSP_LEAVES) $(RSP_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -DN64EMU_RSP_INTERPRETER -I$(RSP)/arch/simd/rsp $(RSP)/jit_tests.cpp $(RSP)/rsp_interpreter.cpp $(RSP_LEAVES) -o $@

benchmarks/usf_checks$(EXE): benchmarks/usf_checks.c src/usf/usf.h
	$(CC) $(CPPFLAGS) $(CFLAGS) benchmarks/usf_checks.c -o $@ $(DL_LIB)

benchmarks/libretro_portable_checks$(EXE): benchmarks/libretro_portable_checks.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(DL_LIB)

# Optional file-level checks; point PSFLIB_DIR at the existing psflib checkout.
ifneq ($(strip $(PSFLIB_DIR)),)
benchmarks/usf_file_checks$(EXE): benchmarks/usf_file_checks.c $(PSFLIB_DIR)/psflib.c $(PSFLIB_DIR)/psflib.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(PSFLIB_DIR) benchmarks/usf_file_checks.c $(PSFLIB_DIR)/psflib.c -lz -lm -o $@ $(DL_LIB)
endif
