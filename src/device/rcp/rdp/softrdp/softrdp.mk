SOFTRDP_DIR := $(CORE_DIR)/src/device/rcp/rdp/softrdp
SOFTRDP_CORE := $(SOFTRDP_DIR)/upstream/core
SOFTRDP_BUILD := $(SOFTRDP_DIR)/build
SOFTRDP_SCALES := 1 2 4
SOFTRDP_SCALED := sr rdp_commands rdp_memory framebuffer raster tmem primitive blend_divider
SOFTRDP_KERNELS := kernel_base vi_base simd_dispatch
SOFTRDP_ISA :=
ifneq (,$(filter x86_64 amd64 x86 i386 i486 i586 i686,$(ARCH)))
SOFTRDP_ISA := -mno-avx -mno-avx2 -mno-fma -mno-sse4.2 -mno-sse4.1 -mno-ssse3 -mno-sse3 -msse2
SOFTRDP_KERNELS += kernel_sse42 vi_sse42
SOFTRDP_DEFINES := -DSOFTRDP_HAVE_SSE42
endif
SOURCES_C += $(SOFTRDP_DIR)/adapter.c $(SOFTRDP_DIR)/thread_bridge.c $(SOFTRDP_DIR)/dps_model.c \
 $(SOFTRDP_CORE)/sr_dispatch.c $(SOFTRDP_DIR)/upstream/present/sr_field_output.c
SOFTRDP_OBJECTS := $(foreach scale,$(SOFTRDP_SCALES),$(addprefix $(SOFTRDP_BUILD)/s$(scale)/,$(addsuffix .o,$(SOFTRDP_SCALED) $(SOFTRDP_KERNELS))))
SOFTRDP_PROFILE ?= 0
SOFTRDP_CONFIG := $(SOFTRDP_DIR)/.build-config
ifneq ($(strip $(file <$(SOFTRDP_CONFIG))),profile=$(SOFTRDP_PROFILE))
$(file >$(SOFTRDP_CONFIG),profile=$(SOFTRDP_PROFILE))
endif
$(SOFTRDP_OBJECTS): $(SOFTRDP_CONFIG)
ifeq ($(SOFTRDP_PROFILE),1)
$(SOFTRDP_OBJECTS): CFLAGS += -g1
endif
$(SOFTRDP_DIR)/adapter.o $(SOFTRDP_DIR)/thread_bridge.o $(SOFTRDP_CORE)/sr_dispatch.o \
 $(SOFTRDP_DIR)/upstream/present/sr_field_output.o: CFLAGS += -std=c17 -O3 $(SOFTRDP_ISA)

define SOFTRDP_SCALE_RULES
$(SOFTRDP_BUILD)/s$(1)/%.o: $(SOFTRDP_CORE)/%.c
	mkdir -p $$(@D)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -std=c17 -O3 $$(SOFTRDP_ISA) $$(SOFTRDP_DEFINES) -DSOFTRDP_SCALE=$(1) -DSR_SCALE_SUFFIX=_s$(1) -include $$(SOFTRDP_CORE)/sr_namespace.h -MMD -MP -c $$< -o $$@
$(SOFTRDP_BUILD)/s$(1)/simd_dispatch.o: $(SOFTRDP_DIR)/simd_dispatch.c
	mkdir -p $$(@D)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -std=c17 -O3 $$(SOFTRDP_ISA) $$(SOFTRDP_DEFINES) -DSOFTRDP_SCALE=$(1) -DSR_SCALE_SUFFIX=_s$(1) -include $$(SOFTRDP_CORE)/sr_namespace.h -MMD -MP -c $$< -o $$@
$(SOFTRDP_BUILD)/s$(1)/%_base.o: $(SOFTRDP_DIR)/%_variant.c
	mkdir -p $$(@D)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -std=c17 -O3 $$(SOFTRDP_ISA) -DSOFTRDP_SCALE=$(1) -DSR_SCALE_SUFFIX=_s$(1) -include $$(SOFTRDP_CORE)/sr_namespace.h -MMD -MP -c $$< -o $$@
$(SOFTRDP_BUILD)/s$(1)/%_sse42.o: $(SOFTRDP_DIR)/%_variant.c
	mkdir -p $$(@D)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -std=c17 -O3 $$(SOFTRDP_ISA) -msse4.2 -DSOFTRDP_FAST_VARIANT -DSOFTRDP_SCALE=$(1) -DSR_SCALE_SUFFIX=_s$(1) -include $$(SOFTRDP_CORE)/sr_namespace.h -MMD -MP -c $$< -o $$@
endef
$(foreach scale,$(SOFTRDP_SCALES),$(eval $(call SOFTRDP_SCALE_RULES,$(scale))))
