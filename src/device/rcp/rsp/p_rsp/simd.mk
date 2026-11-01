# Keep the emitter and dispatch safe on SSE2. Only the leaf implementations
# carry higher ISA flags; calls are resolved when machine code is generated.
RSP_SIMD_OBJECTS :=
ifneq (,$(filter x86_64 amd64,$(ARCH)))
RSP_SIMD_BASE := -mno-avx -mno-avx2 -mno-fma -mno-sse4.2 -mno-sse4.1 -mno-ssse3 -mno-sse3 -msse2
RSP_SIMD_BASE_OBJECTS := $(patsubst %.cpp,%.o,$(filter $(RSPDIR_PARALLEL)/%.cpp,$(SOURCES_CXX)))
$(RSP_SIMD_BASE_OBJECTS): CXXFLAGS += $(RSP_SIMD_BASE) -DN64EMU_RSP_SIMD_DISPATCH
SOURCES_CXX += $(RSPDIR_PARALLEL)/simd_dispatch.cpp
$(RSPDIR_PARALLEL)/simd_dispatch.o: CXXFLAGS += $(RSP_SIMD_BASE) -DN64EMU_RSP_SIMD_DISPATCH
RSP_SIMD_LEAVES := rsp/vfunctions rsp/ls arch/simd/rsp/rsp_core
RSP_SIMD_OBJECTS := $(foreach variant,ssse3 sse41 sse42,$(addprefix $(RSPDIR_PARALLEL)/simd-build/$(variant)/,$(addsuffix .o,$(RSP_SIMD_LEAVES))))
define RSP_SIMD_RULE
$(RSPDIR_PARALLEL)/simd-build/$(1)/%.o: $(RSPDIR_PARALLEL)/%.cpp
	mkdir -p $$(@D)
	$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) $$(RSP_SIMD_BASE) -$(2) -DRSP_SIMD_SUFFIX=_$(1) -include $$(RSPDIR_PARALLEL)/simd_rename.hpp -MMD -MP -c $$< -o $$@
endef
$(eval $(call RSP_SIMD_RULE,ssse3,mssse3))
$(eval $(call RSP_SIMD_RULE,sse41,msse4.1))
$(eval $(call RSP_SIMD_RULE,sse42,msse4.2))
endif
