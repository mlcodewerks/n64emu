cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
project(parallel-rsp LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set(PARALLEL_RSP_CXX_FLAGS -Wall -Wextra -Wno-missing-field-initializers -Wno-empty-body -ffast-math -Wno-unused-parameter)
elseif (MSVC)
    set(PARALLEL_RSP_CXX_FLAGS /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4309 /wd4005 /MP /DNOMINMAX)
endif()

add_library(parallel-rsp STATIC
		rsp/vfunctions.cpp
		rsp_jit.cpp rsp_jit.hpp jit_emitter.hpp jit_vector.cpp
		jit_allocator.cpp jit_allocator.hpp
		rsp_disasm.cpp rsp_disasm.hpp
		rsp/ls.cpp rsp/pipeline.h
		rsp/reciprocal.cpp rsp/reciprocal.h
		rsp/cp0.cpp rsp/cp2.cpp
		arch/simd/rsp/rsp_core.cpp
		arch/simd/rsp/clamp.h
		arch/simd/rsp/rsp_common.h
		arch/simd/rsp/rsp_impl.h
		arch/simd/rsp/vcr.h
		arch/simd/rsp/vabs.h
		arch/simd/rsp/vadd.h
		arch/simd/rsp/vaddc.h
		arch/simd/rsp/vand.h
		arch/simd/rsp/vch.h
		arch/simd/rsp/vcl.h
		arch/simd/rsp/vcr.h
		arch/simd/rsp/vcmp.h
		arch/simd/rsp/vdivh.h
		arch/simd/rsp/vmac.h
		arch/simd/rsp/vmov.h
		arch/simd/rsp/vmrg.h
		arch/simd/rsp/vmudh.h
		arch/simd/rsp/vmul.h
		arch/simd/rsp/vmull.h
		arch/simd/rsp/vmulh.h
		arch/simd/rsp/vmuln.h
		arch/simd/rsp/vor.h
		arch/simd/rsp/vrcpsq.h
		arch/simd/rsp/vrsq.h
		arch/simd/rsp/vsub.h
		arch/simd/rsp/vsubc.h
		arch/simd/rsp/vxor.h
		arch/simd/rsp/vmulm.h)

option(PARALLEL_RSP_DEBUG_JIT "Enable the debug JIT." ON)

if (PARALLEL_RSP_DEBUG_JIT)
	if (ANDROID)
		message("Android does not support debug JIT. Disabling it.")
	elseif (NOT WIN32)
		target_sources(parallel-rsp PRIVATE debug_rsp.cpp debug_rsp.hpp debug_jit.cpp debug_jit.hpp)
		target_compile_definitions(parallel-rsp PUBLIC PARALLEL_RSP_DEBUG_JIT)
	endif()
endif()

target_include_directories(parallel-rsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(parallel-rsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/arch/simd/rsp)
target_compile_options(parallel-rsp PRIVATE ${PARALLEL_RSP_CXX_FLAGS})

get_filename_component(N64EMU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
target_include_directories(parallel-rsp PUBLIC ${N64EMU_ROOT}/deps)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
	target_include_directories(parallel-rsp PUBLIC ${N64EMU_ROOT}/deps/xbyak_aarch64
		${N64EMU_ROOT}/deps/xbyak_aarch64/xbyak_aarch64)
	target_sources(parallel-rsp PRIVATE ${N64EMU_ROOT}/deps/xbyak_aarch64/src/xbyak_aarch64_impl.cpp)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
	target_include_directories(parallel-rsp PUBLIC ${N64EMU_ROOT}/deps/xbyak)
else()
	message(FATAL_ERROR "The RSP Xbyak JIT requires x64 or AArch64.")
endif()
target_compile_definitions(parallel-rsp PUBLIC DEBUG_JIT)

# The emitter stays on the x64 baseline. Compile only vector/memory helpers
# again for higher ISAs and bind their addresses while generating each block.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND
    (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    set(RSP_SIMD_BASE_FLAGS -mno-avx -mno-avx2 -mno-fma -mno-sse4.2
        -mno-sse4.1 -mno-ssse3 -mno-sse3 -msse2)
    target_sources(parallel-rsp PRIVATE simd_dispatch.cpp)
    target_compile_definitions(parallel-rsp PUBLIC N64EMU_RSP_SIMD_DISPATCH)
    target_compile_options(parallel-rsp PUBLIC ${RSP_SIMD_BASE_FLAGS})
    set_target_properties(parallel-rsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    foreach(variant ssse3 sse41 sse42)
        if (variant STREQUAL "ssse3")
            set(variant_flag -mssse3)
        elseif (variant STREQUAL "sse41")
            set(variant_flag -msse4.1)
        else()
            set(variant_flag -msse4.2)
        endif()
        add_library(rsp-${variant}-helpers OBJECT
            rsp/vfunctions.cpp rsp/ls.cpp arch/simd/rsp/rsp_core.cpp)
        target_include_directories(rsp-${variant}-helpers PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/arch/simd/rsp
            ${N64EMU_ROOT}/deps ${N64EMU_ROOT}/deps/xbyak)
        target_compile_definitions(rsp-${variant}-helpers PRIVATE RSP_SIMD_SUFFIX=_${variant})
        target_compile_options(rsp-${variant}-helpers PRIVATE ${PARALLEL_RSP_CXX_FLAGS}
            ${RSP_SIMD_BASE_FLAGS} ${variant_flag}
            -include ${CMAKE_CURRENT_SOURCE_DIR}/simd_rename.hpp)
        set_target_properties(rsp-${variant}-helpers PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_sources(parallel-rsp PRIVATE $<TARGET_OBJECTS:rsp-${variant}-helpers>)
    endforeach()
endif()

option(PARALLEL_RSP_BENCHMARKS "Build the synthetic vector optimizer benchmark." OFF)
if (PARALLEL_RSP_BENCHMARKS)
    # Keep Xbyak's C++-standard-dependent CodeGenerator layout identical in
    # the benchmark and the library, just as for the unit-test executables.
    add_executable(rsp-vector-benchmark ${N64EMU_ROOT}/benchmarks/rsp-optimizer/vector_benchmark.cpp)
    target_link_libraries(rsp-vector-benchmark PRIVATE parallel-rsp)
    target_compile_options(rsp-vector-benchmark PRIVATE ${PARALLEL_RSP_CXX_FLAGS})
endif()

if (NOT WIN32)
	target_link_libraries(parallel-rsp PRIVATE dl)
endif()

add_executable(rsp-runner main.cpp)
target_link_libraries(rsp-runner PRIVATE parallel-rsp)
target_compile_options(rsp-runner PRIVATE ${PARALLEL_RSP_CXX_FLAGS})

if (PARALLEL_RSP_DEBUG_JIT AND NOT WIN32)
	set_target_properties(rsp-runner PROPERTIES LINK_FLAGS "-rdynamic")
endif()

option(PARALLEL_RSP_TESTS "Enable unit tests for the RSP block JIT." ON)
option(PARALLEL_RSP_QUICK_FUZZ "Use bounded native VU fuzzing in CTest instead of the exhaustive run." OFF)
option(PARALLEL_RSP_BUILD_TEST_IMAGES "Assemble RSP test binaries with the MIPS GNU toolchain." ON)
set(PARALLEL_RSP_TEST_IMAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/debug-toolchain" CACHE PATH "Directory containing assembled RSP test images")

add_executable(rsp-vu-fuzzer rsp_vu_fuzzer.cpp)
target_link_libraries(rsp-vu-fuzzer PRIVATE parallel-rsp)
target_compile_options(rsp-vu-fuzzer PRIVATE ${PARALLEL_RSP_CXX_FLAGS})

if (PARALLEL_RSP_TESTS)
	enable_testing()
	add_executable(rsp-jit-tests jit_tests.cpp)
	target_link_libraries(rsp-jit-tests PRIVATE parallel-rsp)
	add_test(NAME rsp-jit-checks COMMAND $<TARGET_FILE:rsp-jit-tests>)
	add_executable(rsp-vector-jit-tests vector_jit_tests.cpp)
	target_link_libraries(rsp-vector-jit-tests PRIVATE parallel-rsp)
	add_test(NAME rsp-vector-jit-checks COMMAND $<TARGET_FILE:rsp-vector-jit-tests>)

	if (ANDROID)
		add_test(NAME rsp-vu-fuzz
				COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/android_fuzz_runner.sh
				${CMAKE_CURRENT_BINARY_DIR}/rsp-vu-fuzzer)
	elseif (PARALLEL_RSP_QUICK_FUZZ)
		# Opt-in for interactive CTest runs. The default exhaustive test and
		# no-argument fuzzer remain unchanged; streaming tools avoid huge logs.
		add_test(NAME rsp-vu-fuzz COMMAND $<TARGET_FILE:rsp-vu-fuzzer> --quick)
	else()
		add_test(NAME rsp-vu-fuzz COMMAND $<TARGET_FILE:rsp-vu-fuzzer>)
	endif()
	add_custom_target(rsp-tests ALL)

	if (PARALLEL_RSP_BUILD_TEST_IMAGES)
		find_program(PARALLEL_RSP_TEST_MAKE NAMES gmake make mingw32-make)
		if (NOT PARALLEL_RSP_TEST_MAKE)
			message(FATAL_ERROR "Assembling RSP tests requires GNU make, or PARALLEL_RSP_BUILD_TEST_IMAGES=OFF with prebuilt test images.")
		endif()
		add_custom_target(rsp-test-crt
			COMMAND ${PARALLEL_RSP_TEST_MAKE} crt-obj -j1
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/debug-toolchain)
	endif()

	function(rsp_add_test NAME)
		if (PARALLEL_RSP_BUILD_TEST_IMAGES)
			add_custom_target(rsp-test-binary-${NAME}
				COMMAND ${PARALLEL_RSP_TEST_MAKE} RSP_TEST=${NAME} -j1
				WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/debug-toolchain
				DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/debug-toolchain/${NAME}.s)
			add_dependencies(rsp-test-binary-${NAME} rsp-test-crt)
			add_dependencies(rsp-tests rsp-test-binary-${NAME})
		endif()

		if (ANDROID)
			add_test(NAME rsp-test-${NAME}
					COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/android_test_runner.sh
					${CMAKE_CURRENT_BINARY_DIR}/rsp-runner
					${PARALLEL_RSP_TEST_IMAGE_DIR} ${NAME})
		else()
			add_test(NAME rsp-test-${NAME}
					COMMAND $<TARGET_FILE:rsp-runner>
					${PARALLEL_RSP_TEST_IMAGE_DIR}/${NAME}.global.bin
					${PARALLEL_RSP_TEST_IMAGE_DIR}/${NAME}.bin)
		endif()
	endfunction()

	rsp_add_test(sll)
	rsp_add_test(srl)
	rsp_add_test(sra)
	rsp_add_test(sllv)
	rsp_add_test(srlv)
	rsp_add_test(srav)
	rsp_add_test(add)
	rsp_add_test(sub)
	rsp_add_test(or)
	rsp_add_test(and)
	rsp_add_test(xor)
	rsp_add_test(nor)
	rsp_add_test(slt)
	rsp_add_test(sltu)
	rsp_add_test(addi)
	rsp_add_test(slti)
	rsp_add_test(sltiu)
	rsp_add_test(andi)
	rsp_add_test(ori)
	rsp_add_test(xori)
	rsp_add_test(lui)
	rsp_add_test(lb)
	rsp_add_test(lbu)
	rsp_add_test(lh)
	rsp_add_test(lhu)
	rsp_add_test(lh-unaligned)
	rsp_add_test(lhu-unaligned)
	rsp_add_test(lw)
	rsp_add_test(lw-unaligned)
	rsp_add_test(sb)
	rsp_add_test(sh)
	rsp_add_test(sw)
	rsp_add_test(sh-unaligned)
	rsp_add_test(sw-unaligned)
	rsp_add_test(beq-impossible-delay-slot-second-taken)
	rsp_add_test(beq-impossible-delay-slot-first-taken)
	#rsp_add_test(beq-impossible-delay-slot-both-taken) Hangs the reference implementation :D
	rsp_add_test(bne)
	rsp_add_test(blez)
	rsp_add_test(bltz)
	rsp_add_test(bltzal)
	rsp_add_test(bgtz)
	rsp_add_test(bgezal)
	rsp_add_test(j)
	rsp_add_test(jal)
	rsp_add_test(jr)
	rsp_add_test(jalr)
	rsp_add_test(lw-unaligned-in-branch-delay)
	rsp_add_test(delay-slot-before-break)
	rsp_add_test(delay-slot-before-new-block)
	rsp_add_test(delay-slot-before-new-block-illegal)
	rsp_add_test(delay-slot-before-new-block-not-taken)
	rsp_add_test(unconditional-delay-slot-before-break)
	rsp_add_test(cop0)
	rsp_add_test(cop2-basic)
	rsp_add_test(cop2-ls)
	rsp_add_test(cop2-vector)
	rsp_add_test(bug-shl-into-branch)
endif()
