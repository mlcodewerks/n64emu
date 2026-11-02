OUT ?= benchmarks/conformance/out/fpu
ifneq (,$(findstring mingw,$(shell $(CC) -dumpmachine)))
EXE := .exe
endif
INC := -Icustom -Isrc -Ideps
FLAGS := -O2 -fno-fast-math -frounding-math $(INC)
.PHONY: check
check: $(OUT)/integer-checks$(EXE)
	$(OUT)/integer-checks$(EXE)
$(OUT):
	mkdir -p $@
$(OUT)/portable.o: src/device/r4300/fpu_native.c src/device/r4300/fpu_native.h src/device/r4300/fpu_neon.h src/device/r4300/cp1.h | $(OUT)
	$(CC) $(FLAGS) -DN64EMU_FPU_PORTABLE -c $< -o $@
$(OUT)/reference.o: src/device/r4300/fpu_native.c src/device/r4300/fpu_native.h src/device/r4300/fpu_neon.h src/device/r4300/cp1.h | $(OUT)
	$(CC) $(FLAGS) -Dfpu_native_eval=fpu_reference_eval -c $< -o $@
$(OUT)/integer-checks$(EXE): benchmarks/conformance/fpu_integer_checks.c $(OUT)/portable.o $(OUT)/reference.o
	$(CC) $(FLAGS) -DFPU_REFERENCE $^ -lm -o $@
