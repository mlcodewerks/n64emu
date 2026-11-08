DEBUG ?= 0
.DEFAULT_GOAL := all
HEADLESS_BENCHMARK ?= 0
FORCE_GLES ?= 0
FORCE_GLES3 ?= 0
LLE ?= 0
HAVE_PARALLEL_RSP ?= 1
HAVE_PARALLEL_RDP ?= 1
HAVE_GLRDP ?= 1
CPU_BACKEND ?= auto
RSP_BACKEND ?= auto
USF ?= 0
ifeq (,$(filter $(CPU_BACKEND),auto interpreter))
$(error CPU_BACKEND must be auto or interpreter)
endif
ifeq (,$(filter $(RSP_BACKEND),auto jit interpreter))
$(error RSP_BACKEND must be auto, jit or interpreter)
endif

SYSTEM_MINIZIP ?= 0
SYSTEM_LIBPNG ?= 0
SYSTEM_XXHASH ?= 0
SYSTEM_ZLIB ?= 0

HAVE_LTCG ?= 0
DYNAFLAGS :=
INCFLAGS  :=
COREFLAGS :=
CPUFLAGS  :=
GLFLAGS   :=
STRINGS   ?= strings
TR        ?= tr

UNAME=$(shell uname -a)

# Dirs
ROOT_DIR := .
DEPSDIR	:=	$(CURDIR)/

ifeq ($(platform),)
   platform = unix
   ifeq ($(UNAME),)
      platform = win
   else ifneq ($(findstring MINGW,$(UNAME)),)
      platform = win
   else ifneq ($(findstring MSYS,$(UNAME)),)
      platform = win
   else ifneq ($(findstring Darwin,$(UNAME)),)
      $(error Only Windows and Linux builds are supported)
   else ifneq ($(findstring win,$(UNAME)),)
      platform = win
   endif
else ifneq (,$(findstring armv,$(platform)))
   override platform += unix
endif

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
   EXE_EXT = .exe
   system_platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   system_platform = win
else ifneq ($(findstring MSYS,$(shell uname -a)),)
   system_platform = win
endif

# Platform aliases select the architecture before compiler/backend detection.
# Prefer an installed MSYS2 toolchain, with the usual cross prefix elsewhere.
# Explicit compiler and CROSS_COMPILE overrides retain their normal precedence.
ifeq ($(platform),win32)
override platform := win
override ARCH := x86
CROSS_COMPILE ?= $(if $(wildcard /mingw32/bin/gcc.exe),/mingw32/bin/,i686-w64-mingw32-)
WINDOWS_TOOLCHAIN_BIN := $(dir $(CROSS_COMPILE))
else ifeq ($(platform),win64)
override platform := win
override ARCH := x86_64
CROSS_COMPILE ?= $(if $(wildcard /mingw64/bin/gcc.exe),/mingw64/bin/,x86_64-w64-mingw32-)
WINDOWS_TOOLCHAIN_BIN := $(dir $(CROSS_COMPILE))
else ifeq ($(platform),linux-i686)
override platform := unix
override ARCH := x86
else ifeq ($(platform),linux-x86_64)
override platform := unix
override ARCH := x86_64
CPUFLAGS += -m64
endif

# GCC's helper executables also need the matching MinGW runtime DLLs.
ifneq (,$(WINDOWS_TOOLCHAIN_BIN))
ifneq ($(WINDOWS_TOOLCHAIN_BIN),./)
export PATH := $(WINDOWS_TOOLCHAIN_BIN):$(PATH)
endif
endif

# Cross compile ?

# Pi 4/5 target 64-bit Raspberry Pi OS, including when cross compiling.
ifneq (,$(filter rpi4 rpi5 rpi4-aarch64 rpi5-aarch64,$(platform)))
ARCH ?= aarch64
ifneq ($(ARCH),aarch64)
$(error Pi 4/5 targets require ARCH=aarch64 and a 64-bit userland)
endif
endif

ifneq ($(CROSS_COMPILE),)
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin AR),default)
AR := $(CROSS_COMPILE)ar
endif
endif

# Compiler names alone do not identify a toolchain: cc/g++ can change through
# PATH or system alternatives. Include both target triples and versions in the
# rebuild stamps, including when only CXX changes to a GCC 9 installation.
CC_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
CXX_MACHINE := $(shell $(CXX) -dumpmachine 2>/dev/null)
CC_VERSION := $(shell $(CC) -dumpfullversion -dumpversion 2>/dev/null || $(CC) -dumpversion 2>/dev/null)
CXX_VERSION := $(shell $(CXX) -dumpfullversion -dumpversion 2>/dev/null || $(CXX) -dumpversion 2>/dev/null)

ifeq (,$(ARCH))
   ARCH = $(firstword $(subst -, ,$(CC_MACHINE)))
   ifeq (,$(ARCH))
      ARCH = $(shell uname -m)
   endif
endif

ifneq (,$(filter amd64,$(ARCH)))
override ARCH := x86_64
endif
ifeq ($(ARCH),arm64)
override ARCH := aarch64
endif

# Normalize explicit 32-bit targets before selecting native emitters.
ifneq (,$(filter x86 i386 i486 i586 i686,$(ARCH)))
override ARCH := x86
CPUFLAGS += -m32 -msse2 -mfpmath=sse
endif
ifneq (,$(filter arm arm32 armv7 armv7l armhf,$(ARCH)))
override ARCH := arm
CPUFLAGS += -march=armv7-a -mfpu=neon -mfloat-abi=hard
endif
ifeq ($(RSP_BACKEND),auto)
RSP_BACKEND := jit
endif
ifeq (,$(filter x86 x86_64 arm aarch64,$(ARCH)))
$(error Unsupported architecture "$(ARCH)"; use x86, x86_64, arm or aarch64)
endif
WITH_DYNAREC ?= $(ARCH)

PIC = 1
TARGET_NAME := n64emu
CC_AS ?= $(CC)
NASM  ?= nasm

GIT_VERSION ?= " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
	COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

ifneq ($(CORE_NAME),)
	COREFLAGS += -DCORE_NAME=\""$(CORE_NAME)"\"
endif

# Linux
ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=$(ROOT_DIR)/src/link.T -Wl,--no-undefined

   ifeq ($(FORCE_GLES),1)
      GLES = 1
      GL_LIB := -lGLESv2
   else ifeq ($(FORCE_GLES3),1)
      GLES3 = 1
      GL_LIB := -lGLESv2
   else
      GL_LIB := -lGL
   endif

   COREFLAGS += -DOS_LINUX
   ifeq ($(ARCH), x86_64)
      ASFLAGS = -f elf64 -d ELF_TYPE
   else
      ASFLAGS = -f elf -d ELF_TYPE
   endif

   ifneq (,$(findstring armv,$(platform)))
      CPUFLAGS += -DARM -marm
      ifneq (,$(findstring cortexa8,$(platform)))
         CPUFLAGS += -mcpu=cortex-a8
      else ifneq (,$(findstring cortexa9,$(platform)))
         CPUFLAGS += -mcpu=cortex-a9
      else
         CPUFLAGS += -mcpu=cortex-a7
      endif
      ifneq (,$(findstring neon,$(platform)))
          CPUFLAGS += -mfpu=neon
          HAVE_NEON = 1
      endif
      ifneq (,$(findstring softfloat,$(platform)))
          CPUFLAGS += -mfloat-abi=softfp
      else ifneq (,$(findstring hardfloat,$(platform)))
          CPUFLAGS += -mfloat-abi=hard
      endif
   endif

# Raspberry Pi 4/5, 64-bit Raspberry Pi OS with Mesa.
else ifneq (,$(filter rpi4 rpi5 rpi4-aarch64 rpi5-aarch64,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=$(ROOT_DIR)/src/link.T -Wl,--no-undefined -ldl
   GLES3 = 1
   GL_LIB := -lGLESv2
   HAVE_NEON = 0
   WITH_DYNAREC = aarch64
   ifneq (,$(findstring rpi5,$(platform)))
      CPUFLAGS += -march=armv8.2-a+crc -mtune=cortex-a76
   else
      CPUFLAGS += -march=armv8-a+crc -mtune=cortex-a72
   endif
   COREFLAGS += -DOS_LINUX

# Windows
else ifeq ($(platform),win)
   ifeq (,$(filter x86 x86_64,$(ARCH)))
      $(error Windows builds require ARCH=x86 or ARCH=x86_64)
   endif
   TARGET := $(TARGET_NAME)_libretro.dll
   LDFLAGS += -shared -Wl,--version-script=$(ROOT_DIR)/src/link.T

   WITH_DYNAREC = $(ARCH)
   ifeq ($(ARCH),x86_64)
      COREFLAGS += -DWIN64 #-DN64EMU_NETPLAY
      ASFLAGS = -f win64 -d WIN64
      PIC = 1
   else ifeq ($(ARCH),x86)
      COREFLAGS += -DWIN32
      LDFLAGS += -static
      PIC = 1
      ASFLAGS = -f win32 -d WIN32 -d LEADING_UNDERSCORE
   endif

   HAVE_PARALLEL_RSP = 1
   HAVE_PARALLEL_RDP = 1
   HAVE_THR_AL = 1
   LLE = 1
   COREFLAGS += -DOS_WINDOWS -DMINGW -DUNICODE
   CXXFLAGS += -fpermissive
else
$(error Unsupported platform "$(platform)"; use win32, win64, win, unix, linux-i686, linux-x86_64, rpi4 or rpi5)
endif

ifeq ($(STATIC_LINKING), 1)
   ifeq ($(platform),win)
      TARGET := $(TARGET:.dll=.lib)
   else
      TARGET := $(TARGET:.so=.a)
   endif
endif

# Legacy devices can also be selected explicitly on a desktop build.
HAVE_GLES2_RDP ?= $(if $(filter 1,$(GLES) $(GLES3) $(FORCE_GLES) $(FORCE_GLES3)),1,0)
HAVE_GL2_RDP ?= $(if $(filter 1,$(GLES) $(GLES3) $(FORCE_GLES) $(FORCE_GLES3)),0,1)
HAVE_D3D9 ?= 0
ifeq ($(USF),1)
override HAVE_PARALLEL_RDP := 0
override HAVE_GLRDP := 0
override HAVE_GL2_RDP := 0
override HAVE_GLES2_RDP := 0
override HAVE_D3D9 := 0
COREFLAGS += -DN64EMU_USF
TARGET := $(subst n64emu_libretro,n64emu_usf,$(TARGET))
ifneq (,$(findstring win,$(platform)))
USF_BUILD_DIR ?= build/usf/$(if $(filter x86_64,$(ARCH)),x64,Win32)
TARGET := $(USF_BUILD_DIR)/$(notdir $(TARGET))
ifneq ($(STATIC_LINKING),1)
# GNU import archives do not support MSVC delay loading. Generate the MSVC
# .lib separately with tools/build_usf_import_library.ps1.
LDFLAGS += -static -Wl,--out-implib,$(TARGET:.dll=.dll.a) -Wl,--output-def,$(TARGET:.dll=.def)
endif
endif
endif

include Makefile.common

ifeq ($(HAVE_NEON), 1)
   COREFLAGS += -DHAVE_NEON -D__ARM_NEON__ -D__NEON_OPT -ftree-vectorize -mvectorize-with-neon-quad -ftree-vectorizer-verbose=2 -funsafe-math-optimizations -fno-finite-math-only
endif

ifeq ($(LLE), 1)
   COREFLAGS += -DHAVE_LLE
endif

COREFLAGS += -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D__LIBRETRO__ -DUSE_FILE32API -D_ENDUSER_RELEASE -DSINC_LOWER_QUALITY -DTXFILTER_LIB -D__VEC4_OPT

ifeq ($(DEBUG), 1)
   CPUOPTS += -O0 -g
   CPUOPTS += -DNDEBUG -fsigned-char -fvisibility=hidden
else
   CPUOPTS += -Ofast
   CPUOPTS += -DNDEBUG -fsigned-char -ffast-math -fno-strict-aliasing -fomit-frame-pointer -fvisibility=hidden
   CXXFLAGS += -fvisibility-inlines-hidden
endif

# Use -fcommon
CPUOPTS += -fcommon

# C++17 is the common baseline for GCC 9 and every renderer.
CFLAGS += -std=gnu99 -D_CRT_SECURE_NO_WARNINGS -Wno-discarded-qualifiers

ifeq ($(HEADLESS_BENCHMARK),1)
CFLAGS += -DN64EMU_HEADLESS_BENCHMARK
endif
CXXFLAGS += -std=c++17 -D_CRT_SECURE_NO_WARNINGS
CXXFLAGS += -fpermissive
# ISA upgrades belong to runtime-dispatched leaves, never the default C++ ABI.

# HLE integer kernels share SSE2 and the existing NEON translation layer.
# Keep this ISA requirement local to HLE; scalar reference builds can opt out.
HLE_SIMD ?= 1
ifeq ($(HLE_SIMD),0)
COREFLAGS += -DHLE_SIMD_DISABLE -DF3D_NO_SSE2 -DRSP_NO_SSE2
else
ifneq (,$(filter x86_64 amd64 x86 i386 i486 i586 i686,$(ARCH)))
HLE_SSE2_FLAGS := -msse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2 -mno-fma -mno-popcnt
$(patsubst %.c,%.o,$(wildcard $(CORE_DIR)/src/device/rcp/rsp/hle/*.c)): CFLAGS += $(HLE_SSE2_FLAGS)
endif
endif

# Rebuild HLE when its ISA contract changes without invalidating unrelated
# CPU and renderer objects. Header dependencies cover the walker layouts.
HLE_SIMD_CONFIG := $(CORE_DIR)/.hle-simd-config
HLE_SIMD_CONFIG_VALUE := arch=$(ARCH):cc=$(CC):$(CC_MACHINE):$(CC_VERSION):simd=$(HLE_SIMD):sse2-neon-v2
ifneq ($(strip $(shell cat "$(HLE_SIMD_CONFIG)" 2>/dev/null)),$(HLE_SIMD_CONFIG_VALUE))
$(file >$(HLE_SIMD_CONFIG),$(HLE_SIMD_CONFIG_VALUE))
endif
$(patsubst %.c,%.o,$(wildcard $(CORE_DIR)/src/device/rcp/rsp/hle/*.c)): $(HLE_SIMD_CONFIG)

ifeq ($(HAVE_LTCG),1)
   CPUFLAGS += -flto
endif

ifeq ($(PIC), 1)
   fpic = -fPIC
else
   fpic = -fno-PIC
endif

# Libretro cores are loaded after frontend/worker startup. Their TLS must not
# consume the loader's static surplus (renderer row caches are intentionally
# much larger). Per-variable signal ownership is handled without TLS access.
SHARED_TLS_FLAGS :=
ifneq ($(platform),win)
ifneq ($(STATIC_LINKING),1)
SHARED_TLS_FLAGS := -ftls-model=global-dynamic
endif
endif

include $(CORE_DIR)/src/device/rcp/rsp/p_rsp/simd.mk
OBJECTS     += $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o) $(SOURCES_ASM:.S=.o) $(SOURCES_NASM:.asm=.o) $(SOFTRDP_OBJECTS) $(RSP_SIMD_OBJECTS)
ifneq ($(platform),win)
COREFLAGS += -D_GNU_SOURCE
endif
# Backend defines affect dispatch and private cache layouts. Rebuild between the
# n64emuJIT emitter, runtime layout and optimization settings.
CPU_JIT_CONFIG := $(CORE_DIR)/.cpu-jit-config
CPU_JIT_CONFIG_VALUE := arch=$(ARCH):platform=$(platform):cc=$(CC):$(CC_MACHINE):$(CC_VERSION):cxx=$(CXX):$(CXX_MACHINE):$(CXX_VERSION):cxxstd=c++17:glrdp=$(HAVE_GLRDP):gl2=$(HAVE_GL2_RDP):gles2=$(HAVE_GLES2_RDP):d3d9=$(HAVE_D3D9):headless=$(HEADLESS_BENCHMARK):usf=$(USF):$(WITH_DYNAREC):cpu=$(CPU_BACKEND):rsp=$(RSP_BACKEND):debug=$(DEBUG):profile=$(N64EMUJIT_PROFILE):hle-simd=$(HLE_SIMD):software=softrdp:simd=sse2-dispatch-v1:pic=$(PIC):tls=$(SHARED_TLS_FLAGS)
# Wrapper invocations must leave the child's configuration stamp intact.
CONFIG_WRAPPERS := x86 arm32 usf usf-x86 usf-arm32 rpi4 rpi5
ifneq ($(strip $(MAKECMDGOALS)),)
ifeq (,$(filter-out $(CONFIG_WRAPPERS),$(MAKECMDGOALS)))
SKIP_CPU_CONFIG := 1
endif
endif
ifneq ($(SKIP_CPU_CONFIG),1)
ifneq ($(strip $(shell cat "$(CPU_JIT_CONFIG)" 2>/dev/null)),$(CPU_JIT_CONFIG_VALUE))
$(file >$(CPU_JIT_CONFIG),$(CPU_JIT_CONFIG_VALUE))
endif
endif
$(OBJECTS): $(CPU_JIT_CONFIG)
CXXFLAGS    += $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS) $(SHARED_TLS_FLAGS)
CFLAGS      += $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS) $(SHARED_TLS_FLAGS)

$(CORE_DIR)/src/device/r4300/cpu_ops.o $(CORE_DIR)/src/device/r4300/jit_runtime.o: CFLAGS += -O3
# Keep precise host operations behind an integer ABI; dispatch and guest checks use fast-math.
$(CORE_DIR)/src/device/r4300/cpu_ops.o: CFLAGS += -ffast-math
$(CORE_DIR)/src/device/r4300/fpu_native.o: CFLAGS += -O3 -fno-fast-math -fno-math-errno -frounding-math -ffp-contract=off

LDFLAGS += -lpthread

LDFLAGS += $(fpic) $(CPUOPTS) $(PLATCFLAGS) $(CPUFLAGS)

-include $(OBJECTS:.o=.d)
all: $(TARGET)

# Convenience targets use the same platform settings as explicit builds.
.PHONY: rpi4 rpi5
rpi4 rpi5:
	$(MAKE) platform=$@ all
.PHONY: x86 arm32 usf usf-x86 usf-arm32
x86:
	$(MAKE) ARCH=x86 HAVE_GLRDP=0 HAVE_PARALLEL_RDP=0 all
arm32:
	$(MAKE) platform=unix ARCH=arm FORCE_GLES=1 HAVE_GLRDP=0 HAVE_PARALLEL_RDP=0 all
usf:
	$(MAKE) USF=1 all
usf-x86:
	$(MAKE) USF=1 ARCH=x86 all
usf-arm32:
	$(MAKE) USF=1 platform=unix ARCH=arm HAVE_PARALLEL_RDP=0 all
$(CORE_DIR)/src/device/r4300/cpu_ops.o: $(addprefix $(CORE_DIR)/src/device/r4300/,fpu_arithmetic.h fpu_compare.h fpu_native.h interp_fpu_move.h)
$(CORE_DIR)/src/device/r4300/jit_runtime.o $(CORE_DIR)/src/device/r4300/cpu_ops.o: $(addprefix $(CORE_DIR)/src/device/r4300/,interp_fpu_move.h interp_thread_poll.h)
$(CORE_DIR)/src/device/r4300/fpu_native.o: $(addprefix $(CORE_DIR)/src/device/r4300/,fpu_native.h fpu_neon.h cp1.h)
$(TARGET): $(OBJECTS)
	mkdir -p $(@D)

ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)
endif

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

ifneq ($(N64EMUJIT_DIR),)
$(N64EMUJIT_SOURCE:.cpp=.o): CXXFLAGS += -O3
$(N64EMUJIT_DIR)/n64emujit_x64.o: $(wildcard $(N64EMUJIT_DIR)/x64/*.inc) $(N64EMUJIT_DIR)/x64/assem_x64.h $(N64EMUJIT_DIR)/x64/xbyak_emit.h $(N64EMUJIT_DIR)/n64emujit.h $(N64EMUJIT_DIR)/code_write_scope.h
$(N64EMUJIT_DIR)/n64emujit_arm64.o: $(wildcard $(N64EMUJIT_DIR)/arm64/*.inc) $(N64EMUJIT_DIR)/arm64/xbyak_emit.h $(N64EMUJIT_DIR)/n64emujit.h $(N64EMUJIT_DIR)/code_write_scope.h
$(N64EMUJIT_DIR)/n64emujit_x86.o $(N64EMUJIT_DIR)/n64emujit_arm32.o: $(wildcard $(N64EMUJIT_DIR)/pair32/*.inc) $(CORE_DIR)/src/jit32/assembler.hpp $(N64EMUJIT_DIR)/n64emujit.h $(N64EMUJIT_DIR)/code_write_scope.h
ifeq ($(N64EMUJIT_PROFILE),1)
$(CORE_DIR)/src/device/r4300/jit_runtime.o: CFLAGS += -DN64EMUJIT_PROFILE
endif
endif

# Cache/RSP state layout and inline timing changes must rebuild their users.
# Missing dependency files are expected on the first build.
-include $(OBJECTS:.o=.d)

clean:
	find -name "*.o" -type f -delete
	find -name "*.d" -type f -delete
	rm -f $(TARGET)

.PHONY: clean
