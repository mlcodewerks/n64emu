CC ?= cc
ROOT := $(dir $(lastword $(MAKEFILE_LIST)))..
CPU := $(ROOT)/src/device/r4300
HOST := $(shell $(CC) -dumpmachine)
OUT ?= $(ROOT)/build/fpu-checks/$(HOST)
ifneq (,$(findstring mingw,$(HOST)))
EXE := .exe
endif
# Standalone native/portable FPU differential check.
.DEFAULT_GOAL := check-fpu
.PHONY: all check check-fpu cpu_fpu_diff
all cpu_fpu_diff: $(OUT)/cpu_fpu_diff$(EXE)
$(OUT):
	mkdir -p $@

check check-fpu: $(OUT)/cpu_fpu_diff$(EXE)
	$<

$(OUT)/cpu_fpu_native.o: $(CPU)/fpu_native.c $(CPU)/fpu_native.h $(CPU)/fpu_neon.h $(CPU)/cp1.h | $(OUT)
	$(CC) -O3 -std=gnu99 $(CFLAGS) -fno-fast-math -fno-math-errno -frounding-math -ffp-contract=off -I$(ROOT)/custom -c $< -o $@

$(OUT)/cpu_fpu_portable.o: $(CPU)/fpu_native.c $(CPU)/fpu_native.h $(CPU)/fpu_neon.h $(CPU)/cp1.h | $(OUT)
	$(CC) -O3 -std=gnu99 $(CFLAGS) -DPARALLAX_FPU_PORTABLE -Dfpu_native_eval=fpu_native_eval_portable -fno-fast-math -fno-math-errno -frounding-math -ffp-contract=off -I$(ROOT)/custom -c $< -o $@

$(OUT)/cpu_fpu_diff$(EXE): $(ROOT)/benchmarks/fpu_native_diff.c $(OUT)/cpu_fpu_native.o $(OUT)/cpu_fpu_portable.o
	$(CC) -O3 -std=gnu99 -ffast-math $(CFLAGS) -I$(ROOT)/src $^ -lm -o $@
