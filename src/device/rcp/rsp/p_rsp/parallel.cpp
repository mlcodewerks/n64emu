#if defined(N64EMU_RSP_INTERPRETER)
#include "rsp_interpreter.hpp"
#elif defined(DEBUG_JIT)
#include "debug_rsp.hpp"
#else
#include "rsp_jit.hpp"
#endif
#include <stdint.h>
#include <cstring>
#include "dpc_bridge.h"

#include "core/backend.h"

#ifdef __LIBRETRO__
#include "renderer_options.h"
extern "C" {
#include "core/log.h"
}
#endif

#define RSP_PARALLEL_VERSION 0x0101
#define RSP_PLUGIN_API_VERSION 0x020000

namespace RSP
{
rsp_memory rsp;
#if defined(N64EMU_RSP_INTERPRETER)
RSP::Interpreter cpu;
#elif defined(DEBUG_JIT)
RSP::CPU cpu;
#else
RSP::JIT::CPU cpu;
#endif
short MFC0_count[32];
int SP_STATUS_TIMEOUT;
} // namespace RSP

extern "C"
{
    void parallelRSPSaveState(uint32_t* words)
    {
        auto& s = RSP::cpu.get_state();
        *words++ = s.pc; *words++ = s.has_delay_slot;
        *words++ = s.branch_target; *words++ = s.cycles;
        *words++ = RSP::SP_STATUS_TIMEOUT;
        for (auto value : s.sr) *words++ = value;
        for (auto& reg : s.cp2.regs) for (auto value : reg.e) *words++ = value;
        for (auto& flag : s.cp2.flags) for (auto value : flag.e) *words++ = value;
        for (auto value : s.cp2.acc.e) *words++ = value;
        *words++ = uint16_t(s.cp2.div_out); *words++ = uint16_t(s.cp2.div_in);
        *words++ = uint8_t(s.cp2.dp_flag);
    }

    void parallelRSPLoadState(const uint32_t* words)
    {
        auto& s = RSP::cpu.get_state();
        s.pc = *words++; s.has_delay_slot = *words++;
        s.branch_target = *words++; s.cycles = *words++;
        s.dma_write_end = 0;
        RSP::SP_STATUS_TIMEOUT = *words++;
        for (auto& value : s.sr) value = *words++;
        for (auto& reg : s.cp2.regs) for (auto& value : reg.e) value = uint16_t(*words++);
        for (auto& flag : s.cp2.flags) for (auto& value : flag.e) value = uint16_t(*words++);
        for (auto& value : s.cp2.acc.e) value = uint16_t(*words++);
        s.cp2.div_out = int16_t(*words++); s.cp2.div_in = int16_t(*words++);
        s.cp2.dp_flag = int8_t(*words++);
        s.dirty_blocks = ~0u;
        RSP::cpu.invalidate_imem();
    }

	// Hack entry point to use when loading savestates when we're tracing.
	void rsp_clear_registers()
	{
		memset(RSP::cpu.get_state().sr, 0, sizeof(uint32_t) * 32);
		memset(&RSP::cpu.get_state().cp2, 0, sizeof(RSP::cpu.get_state().cp2));
	}

#ifdef INTENSE_DEBUG
	// Need super-fast hash here.
	static uint64_t hash_imem(const uint8_t *data, size_t size)
	{
		uint64_t h = 0xcbf29ce484222325ull;
		size_t i;
		for (i = 0; i < size; i++)
			h = (h * 0x100000001b3ull) ^ data[i];
		return h;
	}

	void log_rsp_mem_parallel(void)
	{
		fprintf(stderr, "IMEM HASH: 0x%016llx\n", hash_imem(RSP::rsp.IMEM, 0x1000));
		fprintf(stderr, "DMEM HASH: 0x%016llx\n", hash_imem(RSP::rsp.DMEM, 0x1000));
	}
#endif

    void native_rsp_set_pc(uint32_t pc)
    {
        auto &state = RSP::cpu.get_state();
        state.pc = pc & 0xffc;
        state.has_delay_slot = 0;
        state.branch_target = 0;
        RSP::cpu.invalidate_imem();
    }
	void parallelRSPResetExecutedCycles(void)
	{
		auto &state = RSP::cpu.get_state();
		state.cycles = 0;
		state.dma_write_end = 0;
	}

	unsigned int native_rsp_execute(unsigned int cycles)
	{
        if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT)
			return 0;

		// We don't know if Mupen from the outside invalidated our IMEM.
		RSP::cpu.invalidate_imem();

		// Run CPU until we either break or we need to fire an IRQ.
		RSP::cpu.get_state().pc = *RSP::rsp.SP_PC_REG & 0xfff;
		parallelRSPResetExecutedCycles();

#ifdef INTENSE_DEBUG
		fprintf(stderr, "RUN TASK: %u\n", RSP::cpu.get_state().pc);
		log_rsp_mem_parallel();
#endif

		for (auto &count : RSP::MFC0_count)
			count = 0;

		while (!(*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT))
		{
#ifdef DEBUG_JIT
			auto mode = RSP::cpu.run();
#else
			// Streaming microcode can compute indefinitely without a CP0 poll.
			// Return at a basic-block boundary so CPU events and DMA can progress.
			auto mode = RSP::cpu.run(RSP::cpu.cycle_timing_enabled() ? 32768 : UINT32_MAX);
			if (mode == RSP::MODE_TIMESLICE) break;
#endif
			if (mode == RSP::MODE_CHECK_FLAGS && (*RSP::cpu.get_state().cp0.irq & 1))
				break;
		}

		*RSP::rsp.SP_PC_REG = 0x04001000 | (RSP::cpu.get_state().pc & 0xffc);
		cycles = RSP::cpu.get_state().cycles;

		// From CXD4.
        if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT)
			return cycles;
		else if (*RSP::cpu.get_state().cp0.irq & 1)
			RSP::rsp.CheckInterrupts();
		// Bound subsequent polls even while the semaphore is held: the CPU
		// cannot release it until this synchronous RSP invocation yields.
		RSP::SP_STATUS_TIMEOUT = 16;

		// CPU restarts with the correct SIGs.

		return cycles;
	}

	unsigned int parallelRSPExecutedCycles(void)
	{
		return RSP::cpu.get_state().cycles;
	}
	void native_rsp_close(void)
	{
		*RSP::rsp.SP_PC_REG = 0x00000000;
	}

	void native_rsp_init(rsp_memory Rsp_Info)
	{


		if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
			return; /* DMA is not executed just because plugin initiates. */

		RSP::rsp = Rsp_Info;
		*RSP::rsp.SP_PC_REG = 0x04001000 & 0x00000FFF; /* task init bug on Mupen64 */

		auto **cr = RSP::cpu.get_state().cp0.cr;
		cr[0x0] = RSP::rsp.SP_MEM_ADDR_REG;
		cr[0x1] = RSP::rsp.SP_DRAM_ADDR_REG;
		cr[0x2] = RSP::rsp.SP_RD_LEN_REG;
		cr[0x3] = RSP::rsp.SP_WR_LEN_REG;
		cr[0x4] = RSP::rsp.SP_STATUS_REG;
		cr[0x5] = RSP::rsp.SP_DMA_FULL_REG;
		cr[0x6] = RSP::rsp.SP_DMA_BUSY_REG;
		cr[0x7] = RSP::rsp.SP_SEMAPHORE_REG;
		cr[0x8] = RSP::rsp.DPC_START_REG;
		cr[0x9] = RSP::rsp.DPC_END_REG;
		cr[0xA] = RSP::rsp.DPC_CURRENT_REG;
		cr[0xB] = RSP::rsp.DPC_STATUS_REG;
		cr[0xC] = RSP::rsp.DPC_CLOCK_REG;
		cr[0xD] = RSP::rsp.DPC_BUFBUSY_REG;
		cr[0xE] = RSP::rsp.DPC_PIPEBUSY_REG;
		cr[0xF] = RSP::rsp.DPC_TMEM_REG;

		*cr[RSP::CP0_REGISTER_SP_STATUS] = SP_STATUS_HALT;
		RSP::cpu.get_state().cp0.irq = RSP::rsp.MI_INTR_REG;

		// From CXD4.
		RSP::SP_STATUS_TIMEOUT = 0x7fff;

		RSP::cpu.set_dmem(reinterpret_cast<uint32_t *>(Rsp_Info.DMEM));
		RSP::cpu.set_imem(reinterpret_cast<uint32_t *>(Rsp_Info.IMEM));
		RSP::cpu.set_rdram(reinterpret_cast<uint32_t *>(Rsp_Info.RDRAM));
        native_rsp_set_pc(0);
#if defined(__LIBRETRO__) && !defined(DEBUG_JIT)
        RSP::cpu.set_cycle_timing(renderer_component_timing());
        core_log(CORE_LOG_INFO,"RSP backend: %s; component timing %s",
#ifdef N64EMU_RSP_INTERPRETER
                     "portable LLE",
#else
                     "JIT LLE",
#endif
                     renderer_component_timing() ? "on" : "off");
#endif
	}
}
