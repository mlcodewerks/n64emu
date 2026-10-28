CC ?= cc
ROOT := $(dir $(lastword $(MAKEFILE_LIST)))..
CPU := $(ROOT)/src/device/r4300
include $(ROOT)/benchmarks/cpu-optimizer/checks.mk

cpu_timing: $(OUT)/jit-checks.exe
	cp $< $@

.PHONY: check-fpu
check: check-fpu
check-fpu: cpu_fpu_diff
	./cpu_fpu_diff

cpu_fpu_native.o: $(CPU)/fpu_native.c $(CPU)/fpu_native.h $(CPU)/fpu_neon.h $(CPU)/cp1.h
	$(CC) -O3 -std=gnu99 $(CFLAGS) -fno-fast-math -fno-math-errno -frounding-math -ffp-contract=off -I$(ROOT)/custom -c $< -o $@

cpu_fpu_portable.o: $(CPU)/fpu_native.c $(CPU)/fpu_native.h $(CPU)/fpu_neon.h $(CPU)/cp1.h
	$(CC) -O3 -std=gnu99 $(CFLAGS) -DPARALLAX_FPU_PORTABLE -Dfpu_native_eval=fpu_native_eval_portable -fno-fast-math -fno-math-errno -frounding-math -ffp-contract=off -I$(ROOT)/custom -c $< -o $@

cpu_fpu_diff: $(ROOT)/benchmarks/fpu_native_diff.c cpu_fpu_native.o cpu_fpu_portable.o
	$(CC) -O3 -std=gnu99 -ffast-math $(CFLAGS) -I$(ROOT)/src $^ -lm -o $@
