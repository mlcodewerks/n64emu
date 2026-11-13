OUT ?= tests/.build/extra
PYTHON ?= python
override CFLAGS += -UNDEBUG
override CXXFLAGS += -UNDEBUG
include benchmarks/conformance/rsp_step_checks.mk
include benchmarks/conformance/ai_fifo_checks.mk
.DEFAULT_GOAL := all
.PHONY: all
all: $(OUT)/regressions$(EXE) $(OUT)/step-jit$(EXE) $(OUT)/step-interpreter$(EXE) $(OUT)/signal-checks$(EXE) $(OUT)/ai-fifo-checks$(EXE)
$(OUT)/signal-checks$(EXE): benchmarks/conformance/rsp_signal_checks.cpp $(SOURCES) $(wildcard $(RSP)/*.hpp)
	mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(FLAGS) -DPARALLEL_INTEGRATION $< $(SOURCES) -Wl,--gc-sections $(LDFLAGS) -o $@
$(OUT)/step-jit$(EXE) $(OUT)/step-interpreter$(EXE) $(OUT)/signal-checks$(EXE): $(wildcard $(RSP)/*.inc) src/jit32/assembler.hpp
GPU := src/device/rcp/rdp/glrdp
all: $(OUT)/gpu-parser$(EXE) $(OUT)/fb-notifications$(EXE) $(OUT)/rdp-portable$(EXE) $(OUT)/rdp-short-span$(EXE)
$(OUT):
	mkdir -p $@
$(OUT)/gpu-parser$(EXE): benchmarks/gpu_parser_checks.cpp $(GPU)/rdp_parser.cpp $(GPU)/rdp_tmem.cpp $(wildcard $(GPU)/*.hpp) | $(OUT)
	$(CXX) $(CXXFLAGS) -O2 -std=c++17 -fno-fast-math -Isrc -Ideps -Ideps/libretro-common/include $(filter %.cpp,$^) -o $@
$(OUT)/fb-notifications$(EXE): benchmarks/fb_notification_checks.c src/device/rcp/rdp/fb.c | $(OUT)
	$(CC) $(CFLAGS) -O2 -std=gnu11 -ffunction-sections -fdata-sections -Isrc -Icustom -Ideps/libretro-common/include $^ -Wl,--gc-sections -o $@
$(OUT)/rdp-portable$(EXE): rdp_timing/tests/portable_checks.c $(wildcard rdp_timing/*.h) $(wildcard rdp_timing/tests/*.h) | $(OUT)
	$(CC) $(CFLAGS) -O2 -std=c99 $< -lm -o $@
$(OUT)/rdp-short-span$(EXE): rdp_timing/tests/short_span_vi_checks.c $(wildcard rdp_timing/*.h) $(wildcard rdp_timing/tests/*.h) | $(OUT)
	$(CC) $(CFLAGS) -O2 -std=c99 $< -lm -o $@
define soft_rule
all: $(OUT)/$(1)-s$(2)$(EXE)
$(OUT)/$(1)-s$(2)$(EXE): benchmarks/softrdp_$(3)_checks.c $$(wildcard src/device/rcp/rdp/softrdp/upstream/core/*.[ch]) | $(OUT)
	$$(CC) $$(CFLAGS) -O2 -std=c17 -flto -fwhole-program -DSOFTRDP_SCALE=$(2) $$< -o $$@
endef
$(foreach scale,1 2 4,$(eval $(call soft_rule,scanlines,$(scale),scanline)))
$(foreach scale,1 2 4,$(eval $(call soft_rule,fill-ownership,$(scale),fill_ownership)))
$(foreach scale,1 2 4,$(eval $(call soft_rule,copy-ownership,$(scale),copy_ownership)))
