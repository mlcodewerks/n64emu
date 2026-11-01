#ifdef RSP_JIT_CODE_DUMP
#include "core/file.h"
#endif
#include "rsp_jit.hpp"
#include "rsp/timing.hpp"
#include "rsp/quantize.hpp"
#ifdef PARALLAX_RSP_ENTRY_CAPTURE
#include "../../../../../benchmarks/libdragon/rsp_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void rsp_entry_capture(RSP::CPUState *state, unsigned entry)
{
    static unsigned seen, written;
    const char *prefix=std::getenv("RSP_ENTRY_CAPTURE_PREFIX");
    unsigned stride=std::getenv("RSP_ENTRY_CAPTURE_STRIDE")?std::strtoul(std::getenv("RSP_ENTRY_CAPTURE_STRIDE"),nullptr,0):1;
    unsigned limit=std::getenv("RSP_ENTRY_CAPTURE_LIMIT")?std::strtoul(std::getenv("RSP_ENTRY_CAPTURE_LIMIT"),nullptr,0):16;
    if(!prefix || !stride || seen++%stride || written>=limit)return;
    char path[1024];std::snprintf(path,sizeof(path),"%s-%04u.rsp",prefix,written++);
    FILE *file=std::fopen(path,"wb");if(!file)std::abort();
    libdragon_rsp_fixture f={};f.magic=0x4c445231;f.entry=entry;f.ram_bytes=0x800000;
    f.return_pc=std::strtoul(std::getenv("RSP_ENTRY_RETURN_PC"),nullptr,0);
    std::memcpy(f.scalar,state->sr,sizeof(f.scalar));
    std::memcpy(f.vector,state->cp2.regs,sizeof(f.vector));
    std::memcpy(f.accumulator,state->cp2.acc.e,sizeof(f.accumulator));
    std::memcpy(f.flags,state->cp2.flags,sizeof(f.flags));
    f.div_in=state->cp2.div_in;f.div_out=state->cp2.div_out;f.div_pending=state->cp2.dp_flag;
    for(unsigned i=0;i<16;i++)f.cp0[i]=*state->cp0.cr[i];f.irq=*state->cp0.irq;
    bool ok=std::fwrite(&f,sizeof(f),1,file)==1 && std::fwrite(state->dmem,4096,1,file)==1 &&
        std::fwrite(state->imem,4096,1,file)==1 && std::fwrite(state->rdram,f.ram_bytes,1,file)==1;
    if(std::fclose(file) || !ok)std::abort();
}
#endif
#include "rsp_disasm.hpp"
#include <utility>
#include <assert.h>
#ifdef RSP_JIT_PROFILE
#include <cstdio>
#include <cstdlib>
#endif

using namespace std;

//#define TRACE
//#define TRACE_ENTER
//#define TRACE_DISASM

// These callee-saved registers remain fixed across every block and helper call.
#define JIT_REGISTER_STATE JIT_V0
#define JIT_REGISTER_DMEM JIT_V1
#define JIT_REGISTER_INDIRECT_PC JIT_V2

#define JIT_REGISTER_MODE JIT_R1
#define JIT_REGISTER_NEXT_PC JIT_R0

#define JIT_FRAME_SIZE 256


namespace RSP
{
namespace JIT
{
CPU::CPU()
{
	init_jit_thunks();
}

CPU::~CPU()
{
#ifdef RSP_JIT_PROFILE
    if (const char *path = std::getenv("RSP_JIT_PROFILE_FILE")) {
        if (FILE *file = std::fopen(path,"w")) {
            std::fprintf(file,"overlay,pc,instruction,hits\n");
            for (const auto &p : execution_profiles)
                for (unsigned i=0;i<p->count;++i) if (p->hits[i])
                    for (unsigned j=0;j<p->spans[i];++j)
                        std::fprintf(file,"%016llx,%03x,%08x,%llu\n",
                            (unsigned long long)p->hash,(p->pc+i+j)*4,p->instructions[i+j],
                            (unsigned long long)p->hits[i]);
            std::fclose(file);
        }
    }
#endif
}

void CPU::invalidate_imem()
{
	// Polling redispatches normally keep the same microcode. Compare the whole
	// image once; locate individual changed blocks only when it differs. Keep
	// any dirty bits already raised by an RSP DMA or save-state restore.
	if (!memcmp(cached_imem, state.imem, sizeof(cached_imem)))
		return;
	for (unsigned i = 0; i < CODE_BLOCKS; i++)
		if (memcmp(cached_imem + i * CODE_BLOCK_WORDS, state.imem + i * CODE_BLOCK_WORDS, CODE_BLOCK_SIZE))
			state.dirty_blocks |= (0x3 << i) >> 1;
}

void CPU::invalidate_code()
{
	if (!state.dirty_blocks)
		return;

	for (unsigned i = 0; i < CODE_BLOCKS; i++)
	{
		if (state.dirty_blocks & (1 << i))
		{
			memset(blocks + i * CODE_BLOCK_WORDS, 0, CODE_BLOCK_WORDS * sizeof(blocks[0]));
			memcpy(cached_imem + i * CODE_BLOCK_WORDS, state.imem + i * CODE_BLOCK_WORDS, CODE_BLOCK_SIZE);
		}
	}

	state.dirty_blocks = 0;
}

// Need super-fast hash here.
uint64_t CPU::hash_imem(unsigned pc, unsigned count) const
{
	size_t size = count;

	// FNV-1.
	const auto *data = state.imem + pc;
	uint64_t h = 0xcbf29ce484222325ull;
	h = (h * 0x100000001b3ull) ^ pc;
	h = (h * 0x100000001b3ull) ^ count;
	for (size_t i = 0; i < size; i++)
		h = (h * 0x100000001b3ull) ^ data[i];
	return h;
}

#ifdef TRACE
static uint64_t hash_registers(const CPUState *rsp)
{
	const auto *data = rsp->sr;
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 1; i < 32; i++)
		h = (h * 0x100000001b3ull) ^ data[i];

	data = reinterpret_cast<const uint32_t *>(&rsp->cp2);
	unsigned words = sizeof(rsp->cp2) >> 2;
	for (size_t i = 0; i < words; i++)
		h = (h * 0x100000001b3ull) ^ data[i];

	return h;
}

static uint64_t hash_dmem(const CPUState *rsp)
{
	const auto *data = rsp->dmem;
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < 1024; i++)
		h = (h * 0x100000001b3ull) ^ data[i];
	return h;
}
#endif

unsigned CPU::analyze_static_end(unsigned pc, unsigned end)
{
	// Scans through IMEM and finds the logical "end" of the instruction stream.
	// A logical end of the instruction stream is where execution must terminate.
	// If we have forward branches into this block, i.e. gotos, they extend the execution stream.
	// However, we cannot execute beyond end.
	unsigned max_static_pc = pc;
	unsigned count = end - pc;

	for (unsigned i = 0; i < count; i++)
	{
		uint32_t instr = state.imem[pc + i];
		uint32_t type = instr >> 26;
		uint32_t target;

		bool forward_goto;
		if (pc + i + 1 >= max_static_pc)
		{
			forward_goto = false;
			max_static_pc = pc + i + 1;
		}
		else
			forward_goto = true;

		// VU
		if ((instr >> 25) == 0x25)
			continue;

		switch (type)
		{
		case 000:
			switch (instr & 63)
			{
			case 010:
			case 011:
				// JR and JALR always terminate execution of the block.
				// We execute the next instruction via delay slot and exit.
				// Unless we can branch past the JR
				// (max_static_pc will be higher than expected),
				// this will be the static end.
				if (!forward_goto)
				{
					max_static_pc = max(pc + i + 2, max_static_pc);
					goto end;
				}
				break;

			case 015:
				// BREAK always terminates.
				if (!forward_goto)
					goto end;
				break;

			default:
				break;
			}
			break;

		case 001: // REGIMM
			switch ((instr >> 16) & 31)
			{
			case 000: // BLTZ
			case 001: // BGEZ
			case 021: // BGEZAL
			case 020: // BLTZAL
				// TODO/Optimization: Handle static branch case where $0 is used.
				target = (pc + i + 1 + instr) & 0x3ff;
				if (target >= pc && target < end) // goto
					max_static_pc = max(max_static_pc, target + 1);
				break;

			default:
				break;
			}
			break;

		case 002: // J
		case 003: // JAL
			// Where we choose to end the block here is critical for performance, since otherwise
			// we end up hashing a lot of garbage as it turns out ...

			// J is resolved by goto. Same with JAL if call target happens to be inside the block.
			target = instr & 0x3ff;
			if (target >= pc && target < end) // goto
			{
				// J is a static jump, so if we aren't branching
				// past this instruction and we're branching backwards,
				// we can end the block here.
				if (!forward_goto)
				{
					max_static_pc = max(pc + i + 2, max_static_pc);
					goto end;
				}
				else
					max_static_pc = max(max_static_pc, target + 1);
			}
			else if (!forward_goto)
			{
				// If we have static branch outside our block,
				// we terminate the block.
				max_static_pc = max(pc + i + 2, max_static_pc);
				goto end;
			}
			break;

		case 004: // BEQ
		case 005: // BNE
		case 006: // BLEZ
		case 007: // BGTZ
			// TODO/Optimization: Handle static branch case where $0 is used.
			target = (pc + i + 1 + instr) & 0x3ff;
			if (target >= pc && target < end) // goto
				max_static_pc = max(max_static_pc, target + 1);
			break;

		default:
			break;
		}
	}

end:
	unsigned ret = min(max_static_pc, end);
	return ret;
}

extern "C"
{
#define BYTE_ENDIAN_FIXUP(x, off) ((((x) + (off)) ^ 3) & 0xfffu)
	static Func rsp_enter(void *cpu, unsigned pc)
	{
		return static_cast<CPU *>(cpu)->get_jit_block(pc);
	}

	static HostWord rsp_unaligned_lh(const uint8_t *dram, HostWord addr)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		return HostWord(int16_t((dram[off0] << 8) |
		                          (dram[off1] << 0)));
	}

	static HostWord rsp_unaligned_lw(const uint8_t *dram, HostWord addr)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		auto off2 = BYTE_ENDIAN_FIXUP(addr, 2);
		auto off3 = BYTE_ENDIAN_FIXUP(addr, 3);

		// To sign extend, or not to sign extend, hm ...
		return HostWord((int32_t(dram[off0]) << 24) |
		                  (int32_t(dram[off1]) << 16) |
		                  (int32_t(dram[off2]) << 8) |
		                  (int32_t(dram[off3]) << 0));
	}

	static HostUWord rsp_unaligned_lhu(const uint8_t *dram, HostWord addr)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		return HostWord(uint16_t((dram[off0] << 8) |
		                          (dram[off1] << 0)));
	}

	static void rsp_unaligned_sh(uint8_t *dram, HostWord addr, HostWord data)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		dram[off0] = (data >> 8) & 0xff;
		dram[off1] = (data >> 0) & 0xff;
	}

	static void rsp_unaligned_sw(uint8_t *dram, HostWord addr, HostWord data)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		auto off2 = BYTE_ENDIAN_FIXUP(addr, 2);
		auto off3 = BYTE_ENDIAN_FIXUP(addr, 3);

		dram[off0] = (data >> 24) & 0xff;
		dram[off1] = (data >> 16) & 0xff;
		dram[off2] = (data >> 8) & 0xff;
		dram[off3] = (data >> 0) & 0xff;
	}

#ifdef TRACE
	static void rsp_report_pc(const CPUState *state, HostUWord pc, HostUWord instr)
	{
		auto disasm = disassemble(pc, instr);
		disasm += " (" + std::to_string(hash_registers(state)) + ") (" + std::to_string(hash_dmem(state)) + ")";
		puts(disasm.c_str());
	}
#endif

#ifdef TRACE_ENTER
	static void rsp_report_enter(HostUWord pc)
	{
		printf("  ... Enter 0x%03x ...  ", unsigned(pc & 0xffcu));
	}
#endif
}

void CPU::jit_save_indirect_register(Emitter *_jit, unsigned mips_register)
{
	unsigned jit_reg = regs.load_mips_register_noext(_jit, mips_register);
	_jit->movr(JIT_REGISTER_INDIRECT_PC, jit_reg);
	regs.unlock_mips_register(mips_register);
}

void CPU::jit_save_illegal_indirect_register(Emitter *_jit)
{
	_jit->stxi(-JIT_FRAME_SIZE + 3 * sizeof(HostWord), JIT_FP, JIT_REGISTER_INDIRECT_PC);
}

void CPU::jit_load_indirect_register(Emitter *_jit, unsigned jit_reg)
{
	_jit->movr(jit_reg, JIT_REGISTER_INDIRECT_PC);
}

void CPU::jit_load_illegal_indirect_register(Emitter *_jit, unsigned jit_reg)
{
	_jit->ldxi(jit_reg, JIT_FP, -JIT_FRAME_SIZE + 3 * sizeof(HostWord));
}

void CPU::jit_begin_call(Emitter *_jit)
{
	_jit->prepare();
}

void CPU::jit_end_call(Emitter *_jit, CodePointer ptr)
{
#ifdef PARALLAX_RSP_SIMD_DISPATCH
    ptr = rsp_resolve_simd_helper(ptr);
#endif
	_jit->finishi(ptr);
}

void CPU::jit_save_illegal_cond_branch_taken(Emitter *_jit)
{
	unsigned cond_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
	_jit->stxi(-JIT_FRAME_SIZE + sizeof(HostWord), JIT_FP, cond_reg);
	regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
}

void CPU::jit_restore_illegal_cond_branch_taken(Emitter *_jit, unsigned reg)
{
	_jit->ldxi(reg, JIT_FP, -JIT_FRAME_SIZE + sizeof(HostWord));
}

void CPU::jit_clear_illegal_cond_branch_taken(Emitter *_jit, unsigned tmp_reg)
{
	_jit->movi(tmp_reg, 0);
	_jit->stxi(-JIT_FRAME_SIZE + sizeof(HostWord), JIT_FP, tmp_reg);
}

void CPU::init_jit_thunks()
{
	Emitter emitter;
	Emitter *_jit = &emitter;


	// Saves registers from C++ code.
	_jit->frame(JIT_FRAME_SIZE);
	

	// These registers remain fixed and all called thunks will poke into these registers as necessary.
	_jit->getarg(JIT_REGISTER_STATE);
	_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, pc));
	_jit->ldxi(JIT_REGISTER_DMEM, JIT_REGISTER_STATE, offsetof(CPUState, dmem));

	// When thunks need non-local goto, they jump here.
	auto *entry_label = _jit->indirect();

#ifdef TRACE_ENTER
	{
		// Save PC.
		_jit->stxi_i(offsetof(CPUState, pc), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);
		_jit->prepare();
		_jit->pushargr(JIT_REGISTER_NEXT_PC);
		_jit->finishi(reinterpret_cast<CodePointer>(rsp_report_enter));
		_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, pc));
	}
#endif

	_jit->prepare();
	_jit->pushargr(JIT_REGISTER_STATE);
	_jit->pushargr(JIT_REGISTER_NEXT_PC);
	_jit->finishi(reinterpret_cast<CodePointer>(rsp_enter));
	_jit->retval(JIT_REGISTER_NEXT_PC);

	// Jump to thunk.

	// Clear out branch delay slots.
	jit_clear_illegal_cond_branch_taken(_jit, JIT_REGISTER_MODE);
	_jit->stxi_i(offsetof(CPUState, sr) + RegisterCache::COND_BRANCH_TAKEN * 4, JIT_REGISTER_STATE, JIT_REGISTER_MODE);

	_jit->jmpr(JIT_REGISTER_NEXT_PC);

	// When we want to return, JIT thunks will jump here.
	auto *return_label = _jit->indirect();

	// Save PC.
	_jit->stxi_i(offsetof(CPUState, pc), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);

	// Return status. This register is considered common for all thunks.
	_jit->retr(JIT_REGISTER_MODE);

	HostWord code_size;
	_jit->get_code(&code_size);
	void *thunk_code = allocator.allocate_code(code_size);
	if (!thunk_code)
		abort();
	_jit->set_code(thunk_code, code_size);

	thunks.enter_frame = reinterpret_cast<int (*)(void *)>(_jit->emit());
	thunks.enter_thunk = _jit->address(entry_label);
	thunks.return_thunk = _jit->address(return_label);

	//printf(" === DISASM ===\n");
	//printf(" === END DISASM ===\n");

	if (!Allocator::commit_code(thunk_code, code_size))
		abort();
}

Func CPU::get_jit_block(uint32_t pc)
{
	pc &= IMEM_SIZE - 1;
	uint32_t word_pc = pc >> 2;
	auto &block = blocks[word_pc];

	if (!block)
	{
		unsigned end = (pc + (CODE_BLOCK_SIZE * 2)) >> CODE_BLOCK_SIZE_LOG2;
		end <<= CODE_BLOCK_SIZE_LOG2 - 2;
		end = min(end, unsigned(IMEM_SIZE >> 2));
		end = analyze_static_end(word_pc, end);

		uint64_t hash = hash_imem(word_pc, end - word_pc);
		auto &ptr = cached_blocks[word_pc][hash];
		if (ptr)
			block = ptr;
		else
			block = ptr = jit_region(hash, word_pc, end - word_pc);
	}
	return block;
}

int CPU::enter(uint32_t pc)
{
	// Top level enter.
	state.pc = pc;
	static_assert(offsetof(CPU, state) == 0, "CPU state must lie on first byte.");
	int ret = thunks.enter_frame(this);
	return ret;
}

void CPU::jit_end_of_block(Emitter *_jit, uint32_t pc, const CPU::InstructionInfo &last_info)
{
	// If we run off the end of a block with a pending delay slot, we need to move it to CPUState.
	// We always branch to the next PC, and the delay slot will be handled after the first instruction in next block.

	unsigned cond_branch_reg = 0;
	if (last_info.branch && last_info.conditional)
	{
		cond_branch_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	}
	unsigned scratch_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
	regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	regs.flush_register_window(_jit);

	CodeLabel *forward = nullptr;
	if (last_info.branch)
	{
		if (last_info.conditional)
			forward = _jit->beqi(cond_branch_reg, 0);

		if (last_info.indirect)
			jit_load_indirect_register(_jit, scratch_reg);
		else
			_jit->movi(scratch_reg, last_info.branch_target);
		_jit->stxi_i(offsetof(CPUState, branch_target), JIT_REGISTER_STATE, scratch_reg);
		_jit->movi(scratch_reg, 1);
		_jit->stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, scratch_reg);
	}

	if (forward)
		_jit->patch(forward);
	_jit->movi(JIT_REGISTER_NEXT_PC, pc);
	_jit->jump_absolute(thunks.enter_thunk);
}

void CPU::jit_handle_impossible_delay_slot(Emitter *_jit, const InstructionInfo &info,
                                           const InstructionInfo &last_info, uint32_t base_pc,
                                           uint32_t end_pc)
{
	regs.flush_register_window(_jit);
	jit_branch_stall(_jit, last_info.conditional, false, true);
	unsigned cond_branch_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
	unsigned scratch_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
	unsigned illegal_cond_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER1);

	regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER1);
	regs.flush_register_window(_jit);
	// We can still use the registers after flushing,
	// but we cannot call on the register cache any more until we resolve the branch.

	// A case here would be:
	// beq r0, r1, somewhere
	// beq r1, r2, somewhere
	// <-- we are here ...
	// add r0, r1, r2

	// This case should normally never happen, but you never know what happens on a fixed platform ...
	// Cond branch information for the first branch is found in JIT_FP[-JIT_FRAME_SIZE].
	// Cond branch information for the second branch is found in COND_BRANCH_TAKEN.

	// If the first branch was taken, we will transfer control, but we will never use a local goto here
	// since we potentially need to set the has_delay_slot argument.
	// If the first branch is not taken, we will defer any control transfer until the next instruction, nothing happens,
	// except that FP[0] is cleared.

	CodeLabel *nobranch = nullptr;
	if (last_info.conditional)
	{
		jit_restore_illegal_cond_branch_taken(_jit, illegal_cond_reg);
		jit_clear_illegal_cond_branch_taken(_jit, scratch_reg);
		nobranch = _jit->beqi(illegal_cond_reg, 0);
	}
	else
		jit_clear_illegal_cond_branch_taken(_jit, cond_branch_reg);

	// ... But do we have a delay slot to take care of?
	if (!info.conditional)
		_jit->movi(cond_branch_reg, 1);
	_jit->stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, cond_branch_reg);

	if (info.indirect)
		jit_load_indirect_register(_jit, cond_branch_reg);
	else
		_jit->movi(cond_branch_reg, info.branch_target);
	_jit->stxi_i(offsetof(CPUState, branch_target), JIT_REGISTER_STATE, cond_branch_reg);

	// We are done with register use.

	// Here we *will* take the branch.
	if (last_info.indirect)
		jit_load_illegal_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
	else
		_jit->movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);

	_jit->jump_absolute(thunks.enter_thunk);

	if (nobranch)
		_jit->patch(nobranch);
}

// Taken branches consume a redirect clock after their delay slot (ares RSP
// instructionBranchEpilogue). NEXT_PC has not been selected at these call
// sites. Register state must already be flushed and no cached host register
// may remain live across this helper.
void CPU::jit_branch_stall(Emitter *_jit, bool conditional, bool latent, bool illegal)
{
    if (!cycle_timing) return;
    CodeLabel *skip = nullptr;
    if (conditional) {
        if (illegal) jit_restore_illegal_cond_branch_taken(_jit, JIT_REGISTER_NEXT_PC);
        else _jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE,
            latent ? offsetof(CPUState, has_delay_slot) :
            offsetof(CPUState, sr) + RegisterCache::COND_BRANCH_TAKEN * 4);
        skip = _jit->beqi(JIT_REGISTER_NEXT_PC, 0);
    }
    _jit->ldxi_ui(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, cycles));
    _jit->addi(JIT_REGISTER_NEXT_PC, JIT_REGISTER_NEXT_PC, 1);
    _jit->stxi_i(offsetof(CPUState, cycles), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);
    if (skip) _jit->patch(skip);
}

void CPU::jit_handle_delay_slot(Emitter *_jit, const InstructionInfo &last_info,
                                uint32_t base_pc, uint32_t end_pc)
{
	regs.flush_register_window(_jit);
	jit_branch_stall(_jit, last_info.conditional);
	unsigned scratch_cond_reg = 0;
	if (last_info.conditional)
	{
		regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
		unsigned cond_branch_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);

		scratch_cond_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);

		// Clear out branch state.
		_jit->movr(scratch_cond_reg, cond_branch_reg);
		_jit->movi(cond_branch_reg, 0);

		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	}
	else
	{
		unsigned cond_branch_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		_jit->movi(cond_branch_reg, 0);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	}
	regs.flush_register_window(_jit);

	if (last_info.conditional)
	{
		if (!last_info.indirect && last_info.branch_target >= base_pc && last_info.branch_target < end_pc)
		{
			// Patch this up later.
			unsigned local_index = (last_info.branch_target - base_pc) >> 2;
			local_branches.push_back({ _jit->bnei(scratch_cond_reg, 0), local_index });
		}
		else
		{
			auto *no_branch = _jit->beqi(scratch_cond_reg, 0);
			if (last_info.indirect)
				jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
			else
				_jit->movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
			_jit->jump_absolute(thunks.enter_thunk);
			_jit->patch(no_branch);
		}
	}
	else
	{
		if (!last_info.indirect && last_info.branch_target >= base_pc && last_info.branch_target < end_pc)
		{
			// Patch this up later.
			unsigned local_index = (last_info.branch_target - base_pc) >> 2;
			local_branches.push_back({ _jit->jmpi(), local_index });
		}
		else
		{
			if (last_info.indirect)
				jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
			else
				_jit->movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
			_jit->jump_absolute(thunks.enter_thunk);
		}
	}
}

void CPU::jit_exit(Emitter *_jit, uint32_t pc, const InstructionInfo &last_info,
                   ReturnMode mode, bool first_instruction)
{
	regs.flush_register_window(_jit);
	_jit->movi(JIT_REGISTER_MODE, mode);
	jit_exit_dynamic(_jit, pc, last_info, first_instruction);
}

void CPU::jit_exit_dynamic(Emitter *_jit, uint32_t pc, const InstructionInfo &last_info, bool first_instruction)
{
	if (first_instruction) jit_branch_stall(_jit, true, true);
	else if (last_info.branch) jit_branch_stall(_jit, last_info.conditional);
	// We must not touch REGISTER_MODE / TMP1 here, fortunately we don't need to.
	if (first_instruction)
	{
		// Need to consider that we need to move delay slot to PC.
		_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, has_delay_slot));

		auto *latent_delay_slot = _jit->bnei(JIT_REGISTER_NEXT_PC, 0);

		// Common case.
		// Immediately exit.
		_jit->movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
		_jit->jump_absolute(thunks.return_thunk);

		// If we had a latent delay slot, we handle it here.
		_jit->patch(latent_delay_slot);

		// jit_exit is never called from a branch instruction, so we do not have to handle double branch delay slots here.
		_jit->movi(JIT_REGISTER_NEXT_PC, 0);
		_jit->stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);
		_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, branch_target));
	}
	else if (!last_info.branch)
	{
		// Immediately exit.
		_jit->movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
	}
	else if (!last_info.indirect && !last_info.conditional)
	{
		// Redirect PC to whatever value we were supposed to branch to.
		_jit->movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
	}
	else if (!last_info.conditional)
	{
		// We have an indirect branch, load that register into PC.
		jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
	}
	else if (last_info.indirect)
	{
		// Indirect conditional branch.
		_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE,
		           offsetof(CPUState, sr) + RegisterCache::COND_BRANCH_TAKEN * 4);
		auto *node = _jit->beqi(JIT_REGISTER_NEXT_PC, 0);
		jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
		auto *to_end = _jit->jmpi();
		_jit->patch(node);
		_jit->movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
		_jit->patch(to_end);
	}
	else
	{
		// Direct conditional branch.
		_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE,
		           offsetof(CPUState, sr) + RegisterCache::COND_BRANCH_TAKEN * 4);
		auto *node = _jit->beqi(JIT_REGISTER_NEXT_PC, 0);
		_jit->movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
		auto *to_end = _jit->jmpi();
		_jit->patch(node);
		_jit->movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
		_jit->patch(to_end);
	}

	_jit->jump_absolute(thunks.return_thunk);
}

void CPU::jit_emit_store_operation(Emitter *_jit,
                                   uint32_t pc, uint32_t instr,
                                   void (*jit_emitter)(Emitter *jit, unsigned, unsigned, unsigned), const char *asmop,
                                   CodePointer rsp_unaligned_op,
                                   uint32_t endian_flip,
                                   const InstructionInfo &last_info)
{
	uint32_t align_mask = 3 - endian_flip;
	unsigned rt = (instr >> 16) & 31;
	int16_t simm = int16_t(instr);
	unsigned rs = (instr >> 21) & 31;
	unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
    uint32_t address = 0;
    bool known_address = regs.constant_mips_register(rs, address);
    unsigned rs_reg = known_address ? 0 : regs.load_mips_register_noext(_jit, rs);
	unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
    if (known_address) {
        address = (address + uint32_t(int32_t(simm))) & 0xfffu;
        _jit->movi(rs_tmp_reg, address);
        if (!(address & align_mask)) align_mask = 0;
    } else {
        _jit->addi(rs_tmp_reg, rs_reg, simm);
        _jit->andi(rs_tmp_reg, rs_tmp_reg, 0xfffu);
    }

	// If we are unaligned, it gets very messy to JIT, so just thunk it out to C code.
	CodeLabel *unaligned = nullptr;
	if (align_mask)
	{
		regs.unlock_mips_register(rt);
		if (!known_address) regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
		// We're going to call, so need to save caller-save register we care about.
		regs.flush_caller_save_registers(_jit);

		unaligned = _jit->bmsi(rs_tmp_reg, align_mask);
	}

	// The MIPS is big endian, but the words are swapped per word in integration, so it's kinda little-endian,
	// except we need to XOR the address for byte and half-word accesses.
	if (endian_flip != 0)
		_jit->xori(rs_tmp_reg, rs_tmp_reg, endian_flip);

	jit_emitter(_jit, rs_tmp_reg, JIT_REGISTER_DMEM, rt_reg);

	CodeLabel *aligned = nullptr;
	if (align_mask)
	{
		aligned = _jit->jmpi();
		_jit->patch(unaligned);
		jit_begin_call(_jit);
		_jit->pushargr(JIT_REGISTER_DMEM);
		_jit->pushargr(rs_tmp_reg);
		_jit->pushargr(rt_reg);
		jit_end_call(_jit, rsp_unaligned_op);
		_jit->patch(aligned);
	}
	else
	{
		regs.unlock_mips_register(rt);
		if (!known_address) regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	}
}

// The RSP may or may not have a load-delay slot, but it doesn't seem to matter in practice, so just emulate without
// a load-delay slot.

void CPU::jit_emit_load_operation(Emitter *_jit,
                                  uint32_t pc, uint32_t instr,
                                  void (*jit_emitter)(Emitter *jit, unsigned, unsigned, unsigned), const char *asmop,
                                  CodePointer rsp_unaligned_op,
                                  uint32_t endian_flip,
                                  const InstructionInfo &last_info)
{
	uint32_t align_mask = endian_flip ^ 3;
	unsigned rt = (instr >> 16) & 31;
	if (rt == 0)
		return;

	int16_t simm = int16_t(instr);
	unsigned rs = (instr >> 21) & 31;
    uint32_t address = 0;
    bool known_address = regs.constant_mips_register(rs, address);
    unsigned rs_reg = known_address ? 0 : regs.load_mips_register_noext(_jit, rs);
	unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
    if (known_address) {
        address = (address + uint32_t(int32_t(simm))) & 0xfffu;
        _jit->movi(rs_tmp_reg, address);
        if (!(address & align_mask)) align_mask = 0;
    } else {
        _jit->addi(rs_tmp_reg, rs_reg, simm);
        _jit->andi(rs_tmp_reg, rs_tmp_reg, 0xfffu);
    }

	unsigned ret_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER1);

	// If we are unaligned, it gets very messy to JIT, so just thunk it out to C code.
	CodeLabel *unaligned = nullptr;
	if (align_mask)
	{
		// Flush the register cache here since we might call.
		// We will still use rs_reg/rt_reg, but they only live for this short burst only.
		if (!known_address) regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER1);

		regs.flush_caller_save_registers(_jit);
		unaligned = _jit->bmsi(rs_tmp_reg, align_mask);
	}

	// The MIPS is big endian, but the words are swapped per word in integration, so it's kinda little-endian,
	// except we need to XOR the address for byte and half-word accesses.
	if (endian_flip != 0)
		_jit->xori(rs_tmp_reg, rs_tmp_reg, endian_flip);

	jit_emitter(_jit, ret_reg, JIT_REGISTER_DMEM, rs_tmp_reg);

	CodeLabel *aligned = nullptr;
	if (align_mask)
	{
		aligned = _jit->jmpi();
		_jit->patch(unaligned);
	}

	if (align_mask)
	{
		// We're going to call, so need to save caller-save register we care about.
		jit_begin_call(_jit);
		_jit->pushargr(JIT_REGISTER_DMEM);
		_jit->pushargr(rs_tmp_reg);
		jit_end_call(_jit, rsp_unaligned_op);
		_jit->retval(ret_reg);
		_jit->patch(aligned);
	}
	else
	{
		if (!known_address) regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER1);
	}

	unsigned rt_reg = regs.modify_mips_register(_jit, rt);
	_jit->movr(rt_reg, ret_reg);
	regs.unlock_mips_register(rt);
}

Func CPU::get_vector_sequence(const uint32_t *instructions, unsigned count)
{
    // Exact byte keys avoid relying on hash collision assumptions for code.
    std::string key(reinterpret_cast<const char *>(instructions), count * sizeof(uint32_t));
    auto found = vector_sequences.find(key);
    if (found != vector_sequences.end()) return found->second;
    Emitter emit;
    emit.vector_sequence(instructions, count);
    emit.native_vector_return();
    HostWord size; emit.get_code(&size);
    void *code = allocator.allocate_leaf_code(size); if (!code) abort();
    emit.set_code(code, size); emit.emit(); if (!Allocator::commit_code(code, size)) abort();
    vector_sequences.emplace(std::move(key), code);
    return code;
}

Func CPU::get_vector_block(uint32_t instruction)
{
    unsigned op=instruction&63;
    bool generic=op<0x30 && !quantize_opcode(op);
#ifdef PARALLAX_RSP_JIT32
    // IA-32/AAPCS32 leaves specialize the same optimized vector IR for their
    // smaller register files rather than using the 64-bit leaf argument ABI.
    generic=false;
#endif
    uint32_t key=generic?(0x80000000u|op):instruction;
    auto found=vector_blocks.find(key);
    if(found!=vector_blocks.end())return found->second;
    Emitter emit;
    if(generic)emit.vector_instruction(op,0,0,0,0,true);
    else emit.vector_instruction(op,(instruction>>6)&31,(instruction>>11)&31,(instruction>>16)&31,(instruction>>21)&15);
    emit.native_vector_return();
    HostWord size;emit.get_code(&size);
    void *code=allocator.allocate_leaf_code(size);if(!code)abort();
    emit.set_code(code,size);emit.emit();if(!Allocator::commit_code(code,size))abort();
    vector_blocks.emplace(key,code);return code;
}

#if !defined(__aarch64__) && !defined(_M_ARM64) && !defined(RSP_JIT_EMITTER_AARCH64)
Func CPU::get_quad_partial(bool store)
{
    // COP0 keys cannot collide with the COP2 instruction keys or generic VU
    // keys used by get_vector_block. Reuse its cache without changing layout.
    uint32_t key=0x40000000u|unsigned(store);
    auto found=vector_blocks.find(key);if(found!=vector_blocks.end())return found->second;
    Emitter emit;emit.vector_quad_leaf(JIT_REGISTER_DMEM,store);
    HostWord size;emit.get_code(&size);
    void* code=allocator.allocate_leaf_code(size);if(!code)abort();
    emit.set_code(code,size);emit.emit();if(!Allocator::commit_code(code,size))abort();
    vector_blocks.emplace(key,code);return code;
}
#endif

// Fold in architectural 32-bit arithmetic, before the host register cache's
// sign/zero extensions. Constants describe only the current linear path;
// register-window flushes (joins, delay-slot resolution and exits) kill them.
bool CPU::jit_fold_scalar(Emitter *_jit, uint32_t instr)
{
    unsigned type = instr >> 26, op = instr & 63;
    unsigned rs = (instr >> 21) & 31, rt = (instr >> 16) & 31;
    unsigned rd = (instr >> 11) & 31, shift = (instr >> 6) & 31;
    uint32_t x = 0, y = 0;
    bool kx = regs.constant_mips_register(rs, x), ky = regs.constant_mips_register(rt, y);
    auto constant = [&](unsigned dest, uint32_t value) {
        if (dest) {
            regs.immediate_mips_register(_jit, dest, int32_t(value));
            regs.unlock_mips_register(dest);
        }
        return true;
    };
    auto copy = [&](unsigned dest, unsigned source) {
        if (!dest || dest == source) return true;
        uint32_t value;
        if (regs.constant_mips_register(source, value)) return constant(dest, value);
        unsigned s = regs.load_mips_register_noext(_jit, source);
        unsigned d = regs.modify_mips_register(_jit, dest);
        _jit->movr(d, s);
        regs.unlock_mips_register(source); regs.unlock_mips_register(dest);
        return true;
    };
    if (type == 0) {
        switch (op) {
        case 0: case 2: case 3:
        case 4: case 6: case 7: {
            if (!rd) return true;
            bool variable = op >= 4;
            if (variable && !kx) { if (ky && !y) return constant(rd, 0); return false; }
            unsigned amount = variable ? x & 31 : shift;
            if (ky) return constant(rd, (op == 0 || op == 4) ? y << amount :
                (op == 2 || op == 6) ? y >> amount : uint32_t(int32_t(y) >> amount));
            if (!amount) return copy(rd, rt);
            if (!variable) return false;
            unsigned s = op == 6 ? regs.load_mips_register_zext(_jit, rt) :
                         op == 7 ? regs.load_mips_register_sext(_jit, rt) : regs.load_mips_register_noext(_jit, rt);
            unsigned d = regs.modify_mips_register(_jit, rd);
            if (op == 4) _jit->lshi(d, s, amount);
            else if (op == 6) _jit->rshi_u(d, s, amount);
            else _jit->rshi(d, s, amount);
            regs.unlock_mips_register(rt); regs.unlock_mips_register(rd);
            return true;
        }
        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x24: case 0x25: case 0x26: case 0x27: case 0x2a: case 0x2b: break;
        default: return false;
        }
        if (!rd) return true;
        if (kx && ky) {
            switch (op) {
            case 0x20: case 0x21: return constant(rd, x + y);
            case 0x22: case 0x23: return constant(rd, x - y);
            case 0x24: return constant(rd, x & y);
            case 0x25: return constant(rd, x | y);
            case 0x26: return constant(rd, x ^ y);
            case 0x27: return constant(rd, ~(x | y));
            case 0x2a: return constant(rd, int32_t(x) < int32_t(y));
            case 0x2b: return constant(rd, x < y);
            }
        }
        if (rs == rt) {
            if (op == 0x22 || op == 0x23 || op == 0x26 || op == 0x2a || op == 0x2b) return constant(rd, 0);
            if (op == 0x24 || op == 0x25) return copy(rd, rs);
        }
        if (op == 0x24 && ((kx && !x) || (ky && !y))) return constant(rd, 0);
        if (op == 0x25 && ((kx && x == UINT32_MAX) || (ky && y == UINT32_MAX))) return constant(rd, UINT32_MAX);
        if (op == 0x2b && ky && !y) return constant(rd, 0);
        if ((op == 0x22 || op == 0x23) && ky && !y) return copy(rd, rs);
        bool commutes = op == 0x20 || op == 0x21 || op == 0x24 || op == 0x25 || op == 0x26;
        if (commutes && kx) { std::swap(rs, rt); std::swap(x, y); std::swap(kx, ky); }
        if (!ky) return false;
        if ((commutes && !y && op != 0x24) || (op == 0x24 && y == UINT32_MAX)) return copy(rd, rs);
        if (!commutes && op != 0x22 && op != 0x23 && op != 0x2a && op != 0x2b) return false;
        unsigned s = op == 0x2a ? regs.load_mips_register_sext(_jit, rs) :
                     op == 0x2b ? regs.load_mips_register_zext(_jit, rs) : regs.load_mips_register_noext(_jit, rs);
        unsigned d = regs.modify_mips_register(_jit, rd);
        switch (op) {
        case 0x20: case 0x21: _jit->addi(d, s, int32_t(y)); break;
        // Negate as unsigned to avoid overflow for INT32_MIN.
        case 0x22: case 0x23: _jit->addi(d, s, int32_t(0u - y)); break;
        case 0x24: _jit->andi(d, s, int32_t(y)); break;
        case 0x25: _jit->ori(d, s, int32_t(y)); break;
        case 0x26: _jit->xori(d, s, int32_t(y)); break;
        case 0x2a: _jit->lti(d, s, int32_t(y)); break;
        case 0x2b: _jit->lti_u(d, s, HostWord(y)); break;
        }
        regs.unlock_mips_register(rs); regs.unlock_mips_register(rd);
        return true;
    }
    if (type < 8 || type > 15) return false;
    if (!rt) return true;
    uint32_t imm = uint32_t(int32_t(int16_t(instr)));
    if (type >= 12) imm = uint16_t(instr);
    if (type == 15) return constant(rt, imm << 16);
    // Keep SLTIU's existing RSP/reference immediate interpretation.
    if (type == 11) imm = uint16_t(instr);
    if (kx) {
        switch (type) {
        case 8: case 9: return constant(rt, x + imm);
        case 10: return constant(rt, int32_t(x) < int32_t(imm));
        case 11: return constant(rt, x < imm);
        case 12: return constant(rt, x & imm);
        case 13: return constant(rt, x | imm);
        case 14: return constant(rt, x ^ imm);
        }
    }
    if (type == 12 && !imm) return constant(rt, 0);
    if ((type == 8 || type == 9 || type == 13 || type == 14) && !imm) return copy(rt, rs);
    if (type == 11 && !imm) return constant(rt, 0);
    return false;
}

void CPU::jit_instruction(Emitter *_jit, uint32_t pc, uint32_t instr,
                          InstructionInfo &info, const InstructionInfo &last_info,
                          bool first_instruction, bool next_instruction_is_branch_target)
{
#ifdef PARALLAX_RSP_ENTRY_CAPTURE
    const char *capture_pc=std::getenv("RSP_ENTRY_CAPTURE_PC");
    if(capture_pc && pc==std::strtoul(capture_pc,nullptr,0)) {
        regs.flush_register_window(_jit);
        jit_begin_call(_jit);_jit->pushargr(JIT_REGISTER_STATE);_jit->pushargi(pc);
        jit_end_call(_jit,reinterpret_cast<CodePointer>(rsp_entry_capture));
    }
#endif
#ifdef TRACE
	regs.flush_register_window(_jit);
	jit_begin_call(_jit);
	_jit->pushargr(JIT_REGISTER_STATE);
	_jit->pushargi(pc);
	_jit->pushargi(instr);
	jit_end_call(_jit, reinterpret_cast<CodePointer>(rsp_report_pc));
#endif

    if (jit_fold_scalar(_jit, instr)) return;

    // Keep the original branch and delay-slot bookkeeping, but replace a
    // proven condition with a literal. Never remove a guest delay slot.
    {
        unsigned type = instr >> 26, rs = (instr >> 21) & 31, rt = (instr >> 16) & 31;
        uint32_t x = 0, y = 0;
        bool kx = regs.constant_mips_register(rs, x), ky = regs.constant_mips_register(rt, y);
        bool known = false, taken = false, link = false;
        if ((type == 4 || type == 5) && ((kx && ky) || rs == rt)) {
            known = true; taken = type == 4 ? (rs == rt || x == y) : (rs != rt && x != y);
        } else if ((type == 6 || type == 7) && kx) {
            known = true; taken = type == 6 ? int32_t(x) <= 0 : int32_t(x) > 0;
        } else if (type == 1 && kx && (rt == 0 || rt == 1 || rt == 16 || rt == 17)) {
            known = true; taken = (rt & 1) ? int32_t(x) >= 0 : int32_t(x) < 0; link = (rt & 16) != 0;
        }
        if (known) {
            if (last_info.branch && last_info.conditional) jit_save_illegal_cond_branch_taken(_jit);
            if (last_info.branch && last_info.indirect) jit_save_illegal_indirect_register(_jit);
            regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, taken);
            regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
            if (link) {
                regs.immediate_mips_register(_jit, 31, (pc + 8) & 0xffcu);
                regs.unlock_mips_register(31);
            }
            info.branch = info.conditional = true;
            info.branch_target = (pc + 4 + (instr << 2)) & 0xffc;
            return;
        }
    }

	// VU
	if ((instr >> 25) == 0x25)
	{
		// VU instruction. COP2, and high bit of opcode is set.
		uint32_t op = instr & 63;
		// VNOP has no vector/accumulator effects. Timing was already decoded
		// above; avoid a helper call and caller-save spills in either mode.
		if (op == 0x37 || op == 0x3f)
			return;
		uint32_t vd = (instr >> 6) & 31;
		uint32_t vs = (instr >> 11) & 31;
		uint32_t vt = (instr >> 16) & 31;
		uint32_t e = (instr >> 21) & 15;

		// Divider normalization uses volatile scalar scratch registers. Other
		// vector instructions leave the scalar register cache intact.
		if (op == 0x30 || op == 0x31 || op == 0x34 || op == 0x35 || quantize_opcode(op))
			regs.flush_caller_save_registers(_jit);
        // Large arithmetic sequences share native leaves across overlapping
        // regions and microcodes. Their private ABI keeps the scalar cache live.
        bool arithmetic=op<=1 || (op>=4 && op<=9) || (op>=0x0c && op<=0x11) || (op>=0x13 && op<=0x15);
        bool divider=op==0x30 || op==0x31 || op==0x34 || op==0x35;
        if(arithmetic || (op>=0x20 && op<=0x26) || divider) {
            if(!divider)_jit->vector_arguments(vd,vs,vt,e);
            _jit->native_vector_call(get_vector_block(instr));
        } else
            _jit->vector_instruction(op, vd, vs, vt, e);
		return;
	}

	uint32_t type = instr >> 26;

#define NOP_IF_RD_ZERO() if (rd == 0) { break; }
#define NOP_IF_RT_ZERO() if (rt == 0) { break; }

	switch (type)
	{
	case 000:
	{
		auto rd = (instr >> 11) & 31;
		auto rt = (instr >> 16) & 31;
		auto shift = (instr >> 6) & 31;
		auto rs = (instr >> 21) & 31;

		switch (instr & 63)
		{
		case 000: // SLL
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			_jit->lshi(rd_reg, rt_reg, shift);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			break;
		}

		case 002: // SRL
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_zext(_jit, rt);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			_jit->rshi_u(rd_reg, rt_reg, shift);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			break;
		}

		case 003: // SRA
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			_jit->rshi(rd_reg, rt_reg, shift);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			break;
		}

		case 004: // SLLV
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			_jit->andi(rs_tmp_reg, rs_reg, 31);
			regs.unlock_mips_register(rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			_jit->lshr(rd_reg, rt_reg, rs_tmp_reg);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			break;
		}

		case 006: // SRLV
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_zext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			_jit->andi(rs_tmp_reg, rs_reg, 31);
			regs.unlock_mips_register(rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			_jit->rshr_u(rd_reg, rt_reg, rs_tmp_reg);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			break;
		}

		case 007: // SRAV
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			_jit->andi(rs_tmp_reg, rs_reg, 31);
			regs.unlock_mips_register(rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			_jit->rshr(rd_reg, rt_reg, rs_tmp_reg);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			break;
		}

		// If the last instruction is also a branch instruction, we will need to do some funky handling
		// so make sure we save the old branch taken register.
#define FLUSH_IMPOSSIBLE_DELAY_SLOT() do { \
	if (last_info.branch && last_info.conditional) \
		jit_save_illegal_cond_branch_taken(_jit); \
	if (last_info.branch && last_info.indirect) \
		jit_save_illegal_indirect_register(_jit); \
	} while(0)

		case 010: // JR
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			info.branch = true;
			info.indirect = true;
			jit_save_indirect_register(_jit, rs);

			// If someone can branch to the delay slot, we have to turn this into a conditional branch.
			if (next_instruction_is_branch_target)
			{
				info.conditional = true;
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
			}
			else
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
			break;
		}

		case 011: // JALR
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			jit_save_indirect_register(_jit, rs);
			if (rd != 0)
			{
				regs.immediate_mips_register(_jit, rd, (pc + 8) & 0xffcu);
				regs.unlock_mips_register(rd);
			}

			info.branch = true;
			info.indirect = true;
			// If someone can branch to the delay slot, we have to turn this into a conditional branch.
			if (next_instruction_is_branch_target)
			{
				info.conditional = true;
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
			}
			else
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
			break;
		}

		case 015: // BREAK
		{
			jit_exit(_jit, pc, last_info, MODE_BREAK, first_instruction);
			info.handles_delay_slot = true;
			break;
		}

#define THREE_REG_OP(op, ext) \
	NOP_IF_RD_ZERO(); \
	unsigned rs_reg = regs.load_mips_register_##ext(_jit, rs); \
	unsigned rt_reg = regs.load_mips_register_##ext(_jit, rt); \
	unsigned rd_reg = regs.modify_mips_register(_jit, rd); \
	_jit->op(rd_reg, rs_reg, rt_reg); \
	regs.unlock_mips_register(rs); \
	regs.unlock_mips_register(rt); \
	regs.unlock_mips_register(rd)

		case 040: // ADD
		case 041: // ADDU
		{
			THREE_REG_OP(addr, noext);
			break;
		}

		case 042: // SUB
		case 043: // SUBU
		{
			THREE_REG_OP(subr, noext);
			break;
		}

		case 044: // AND
		{
			THREE_REG_OP(andr, noext);
			break;
		}

		case 045: // OR
		{
			THREE_REG_OP(orr, noext);
			break;
		}

		case 046: // XOR
		{
			THREE_REG_OP(xorr, noext);
			break;
		}

		case 047: // NOR
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			_jit->orr(rd_reg, rt_reg, rs_reg);
			_jit->xori(rd_reg, rd_reg, HostWord(-1));
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(rd);
			break;
		}

		case 052: // SLT
		{
			THREE_REG_OP(ltr, sext);
			break;
		}

		case 053: // SLTU
		{
			THREE_REG_OP(ltr_u, zext);
			break;
		}

		default:
			break;
		}
		break;
	}

	case 001: // REGIMM
	{
		unsigned rt = (instr >> 16) & 31;

		switch (rt)
		{
		case 020: // BLTZAL
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			_jit->lti(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			// Link register is written after condition.
			regs.immediate_mips_register(_jit, 31, (pc + 8) & 0xffcu);
			regs.unlock_mips_register(31);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		case 000: // BLTZ
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;

			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			_jit->lti(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		case 021: // BGEZAL
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			_jit->gei(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			// Link register is written after condition.
			regs.immediate_mips_register(_jit, 31, (pc + 8) & 0xffcu);
			regs.unlock_mips_register(31);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		case 001: // BGEZ
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			_jit->gei(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		default:
			break;
		}
		break;
	}

	case 003: // JAL
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		uint32_t target_pc = (instr & 0x3ffu) << 2;
		regs.immediate_mips_register(_jit, 31, (pc + 8) & 0xffcu);

		info.branch = true;
		info.branch_target = target_pc;
		if (next_instruction_is_branch_target)
		{
			info.conditional = true;
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
		}
		else
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

		regs.unlock_mips_register(31);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		break;
	}

	case 002: // J
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		uint32_t target_pc = (instr & 0x3ffu) << 2;

		info.branch = true;
		info.branch_target = target_pc;
		if (next_instruction_is_branch_target)
		{
			info.conditional = true;
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
		}
		else
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		break;
	}

	case 004: // BEQ
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;
		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
		unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
		unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
		unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		_jit->eqr(cond_reg, rs_reg, rt_reg);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(rt);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		info.branch = true;
		info.conditional = true;
		info.branch_target = target_pc;
		break;
	}

	case 005: // BNE
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;
		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
		unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
		unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
		unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		_jit->ner(cond_reg, rs_reg, rt_reg);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(rt);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		info.branch = true;
		info.conditional = true;
		info.branch_target = target_pc;
		break;
	}

	case 006: // BLEZ
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;
		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;

		// If using $0, it's an unconditional branch.
		if (rs != 0)
		{
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			_jit->lei(cond_reg, rs_reg, 0);
			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
			info.conditional = true;
		}

		info.branch = true;
		info.branch_target = target_pc;
		break;
	}

	case 007: // BGTZ
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;

		// Meaningless
		if (rs == 0)
			break;

		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
		unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
		unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		_jit->gti(cond_reg, rs_reg, 0);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

		info.branch = true;
		info.conditional = true;
		info.branch_target = target_pc;
		break;
	}

#define TWO_REG_RS_IS_ZERO() (((instr >> 21) & 31) == 0)

#define TWO_REG_IMM_OP(op, immtype, ext) \
	unsigned rt = (instr >> 16) & 31; \
	NOP_IF_RT_ZERO(); \
	unsigned rs = (instr >> 21) & 31; \
	unsigned rs_reg = regs.load_mips_register_##ext(_jit, rs); \
	unsigned rt_reg = regs.modify_mips_register(_jit, rt); \
	_jit->op(rt_reg, rs_reg, immtype(instr)); \
	regs.unlock_mips_register(rs); \
	regs.unlock_mips_register(rt)

	case 010: // ADDI
	case 011:
	{
		if (TWO_REG_RS_IS_ZERO())
		{
			unsigned rt = (instr >> 16) & 31;
			NOP_IF_RT_ZERO();
			regs.immediate_mips_register(_jit, rt, int16_t(instr));
			regs.unlock_mips_register(rt);
		}
		else
		{
			TWO_REG_IMM_OP(addi, int16_t, noext);
		}
		break;
	}

	case 012: // SLTI
	{
		TWO_REG_IMM_OP(lti, int16_t, sext);
		break;
	}

	case 013: // SLTIU
	{
		TWO_REG_IMM_OP(lti_u, uint16_t, zext);
		break;
	}

	case 014: // ANDI
	{
		TWO_REG_IMM_OP(andi, uint16_t, noext);
		break;
	}

	case 015: // ORI
	{
		if (TWO_REG_RS_IS_ZERO())
		{
			unsigned rt = (instr >> 16) & 31;
			NOP_IF_RT_ZERO();
			regs.immediate_mips_register(_jit, rt, uint16_t(instr));
			regs.unlock_mips_register(rt);
		}
		else
		{
			TWO_REG_IMM_OP(ori, uint16_t, noext);
		}
		break;
	}

	case 016: // XORI
	{
		TWO_REG_IMM_OP(xori, uint16_t, noext);
		break;
	}

	case 017: // LUI
	{
		unsigned rt = (instr >> 16) & 31;
		NOP_IF_RT_ZERO();
		int16_t imm = int16_t(instr);
		regs.immediate_mips_register(_jit, rt, imm << 16);
		regs.unlock_mips_register(rt);
		break;
	}

	case 020: // COP0
	{
		unsigned rd = (instr >> 11) & 31;
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;

		switch (rs)
		{
		case 000: // MFC0
		{
			regs.flush_register_window(_jit);

			jit_begin_call(_jit);
			_jit->pushargr(JIT_REGISTER_STATE);
			_jit->pushargi(rt);
			_jit->pushargi(rd);
			jit_end_call(_jit, reinterpret_cast<CodePointer>(RSP_MFC0));
			_jit->retval(JIT_REGISTER_MODE);

			CodeLabel *noexit = _jit->beqi(JIT_REGISTER_MODE, MODE_CONTINUE);
			jit_exit_dynamic(_jit, pc, last_info, first_instruction);
			_jit->patch(noexit);

			break;
		}

		case 004: // MTC0
		{
			regs.flush_register_window(_jit);

			jit_begin_call(_jit);
			_jit->pushargr(JIT_REGISTER_STATE);
			_jit->pushargi(rd);
			_jit->pushargi(rt);
			jit_end_call(_jit, reinterpret_cast<CodePointer>(RSP_MTC0));
			_jit->retval(JIT_REGISTER_MODE);

			CodeLabel *noexit = _jit->beqi(JIT_REGISTER_MODE, MODE_CONTINUE);
			jit_exit_dynamic(_jit, pc, last_info, first_instruction);
			_jit->patch(noexit);

			break;
		}

		default:
			break;
		}
		break;
	}

	case 022: // COP2
	{
		unsigned rd = (instr >> 11) & 31;
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;
		unsigned imm = (instr >> 7) & 15;

		if (rs == 0 || rs == 2) {
			if (rt) {
				unsigned dest = regs.modify_mips_register(_jit, rt);
				_jit->vector_transfer(rs, dest, rd, imm);
				regs.unlock_mips_register(rt);
			}
		} else if (rs == 4 || rs == 6) {
			unsigned source = regs.load_mips_register_noext(_jit, rt);
			_jit->vector_transfer(rs, source, rd, imm);
			regs.unlock_mips_register(rt);
		}
		break;
	}

	case 040: // LB
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->ldxr_c(a, b, c); },
		                        "lb",
		                        nullptr,
		                        3, last_info);
		break;
	}

	case 041: // LH
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->ldxr_s(a, b, c); },
		                        "lh",
		                        reinterpret_cast<CodePointer>(rsp_unaligned_lh),
		                        2, last_info);
		break;
	}

	case 043: // LW
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->ldxr_i(a, b, c); },
		                        "lw",
		                        reinterpret_cast<CodePointer>(rsp_unaligned_lw),
		                        0, last_info);
		break;
	}

	case 044: // LBU
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->ldxr_uc(a, b, c); },
		                        "lbu",
		                        nullptr,
		                        3, last_info);
		break;
	}

	case 045: // LHU
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->ldxr_us(a, b, c); },
		                        "lhu",
		                        reinterpret_cast<CodePointer>(rsp_unaligned_lhu),
		                        2, last_info);
		break;
	}

	case 050: // SB
	{
		jit_emit_store_operation(_jit, pc, instr,
		                         [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->stxr_c(a, b, c); },
		                         "sb",
		                         nullptr,
		                         3, last_info);
		break;
	}

	case 051: // SH
	{
		jit_emit_store_operation(_jit, pc, instr,
		                         [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->stxr_s(a, b, c); },
		                         "sh",
		                         reinterpret_cast<CodePointer>(rsp_unaligned_sh),
		                         2, last_info);
		break;
	}

	case 053: // SW
	{
		jit_emit_store_operation(_jit, pc, instr,
		                         [](Emitter *_jit, unsigned a, unsigned b, unsigned c) { _jit->stxr_i(a, b, c); },
		                         "sh",
		                         reinterpret_cast<CodePointer>(rsp_unaligned_sw),
		                         0, last_info);
		break;
	}

	case 062: // LWC2
	{
        if(jit_vector_memory(_jit,instr,false))break;
		unsigned rt = (instr >> 16) & 31;
		int16_t simm = instr;
		// Sign-extend.
		simm <<= 9;
		simm >>= 9;
		unsigned rs = (instr >> 21) & 31;
		unsigned rd = (instr >> 11) & 31;
		unsigned imm = (instr >> 7) & 15;

		using LWC2Op = void (*)(RSP::CPUState *, unsigned rt, unsigned imm, int simm, unsigned rs);
		static const LWC2Op ops[32] = {
			RSP_LBV, RSP_LSV, RSP_LLV, RSP_LDV, RSP_LQV, RSP_LRV, RSP_LPV, RSP_LUV, RSP_LHV, RSP_LFV, nullptr, RSP_LTV,
		};

		auto *op = ops[rd];
		if (op)
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rs);
			jit_begin_call(_jit);
			_jit->pushargr(JIT_REGISTER_STATE);
			_jit->pushargi(rt);
			_jit->pushargi(imm);
			_jit->pushargi(simm);
			_jit->pushargi(rs);
			jit_end_call(_jit, reinterpret_cast<CodePointer>(op));
		}
		break;
	}

	case 072: // SWC2
	{
        if(jit_vector_memory(_jit,instr,true))break;
		unsigned rt = (instr >> 16) & 31;
		int16_t simm = instr;
		// Sign-extend.
		simm <<= 9;
		simm >>= 9;
		unsigned rs = (instr >> 21) & 31;
		unsigned rd = (instr >> 11) & 31;
		unsigned imm = (instr >> 7) & 15;

		using SWC2Op = void (*)(RSP::CPUState *, unsigned rt, unsigned imm, int simm, unsigned rs);
		static const SWC2Op ops[32] = {
			RSP_SBV, RSP_SSV, RSP_SLV, RSP_SDV, RSP_SQV, RSP_SRV, RSP_SPV, RSP_SUV, RSP_SHV, RSP_SFV, RSP_SWV, RSP_STV,
		};

		auto *op = ops[rd];
		if (op)
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rs);
			jit_begin_call(_jit);
			_jit->pushargr(JIT_REGISTER_STATE);
			_jit->pushargi(rt);
			_jit->pushargi(imm);
			_jit->pushargi(simm);
			_jit->pushargi(rs);
			jit_end_call(_jit, reinterpret_cast<CodePointer>(op));
		}
		break;
	}

	default:
		break;
	}
}

bool CPU::jit_vector_memory(Emitter *_jit,uint32_t instr,bool store)
{
#ifdef TRACE
    return false;
#endif
    unsigned type=(instr>>11)&31,element=(instr>>7)&15;
    bool packed=!store && (type==6 || type==7) && !element;
    if(type>4 && !packed)return false;
    unsigned size=packed?8:1u<<type;
    bool quad_native=type==4 && Emitter::native_quad_partial();
    if((size>1 && (element&1)) || element+size>16)return false;
    unsigned rs=(instr>>21)&31,vt=(instr>>16)&31;
    int offset=(int32_t(instr<<25)>>25)*int(size);
    uint32_t base=0;bool known=regs.constant_mips_register(rs,base);
    if(known && ((base+offset)&(size-1)) && !quad_native)return false;
    unsigned address;
    if(known)address=regs.immediate_mips_register(_jit,RegisterCache::SCRATCH_REGISTER0,(base+offset)&4095);
    else {
        unsigned source=regs.load_mips_register_noext(_jit,rs);
        address=regs.modify_mips_register(_jit,RegisterCache::SCRATCH_REGISTER0);
        _jit->addi(address,source,offset);_jit->andi(address,address,4095);
        regs.unlock_mips_register(rs);
    }
    regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
    bool fallback=!known && size>1 && !quad_native;
    if(fallback){regs.flush_caller_save_registers(_jit);regs.flush_mips_register(_jit,rs);}
    auto *unaligned=fallback?_jit->bmsi(address,size-1):nullptr;
    HostWord vector_offset=offsetof(CPUState,cp2.regs)+vt*sizeof(state.cp2.regs[0])+(size==1?MES(element):element);
    auto partial_quad=[&]() {
#if defined(__aarch64__) || defined(_M_ARM64) || defined(RSP_JIT_EMITTER_AARCH64)
        _jit->vector_quad_partial(JIT_REGISTER_STATE,JIT_REGISTER_DMEM,address,vector_offset,store);
#else
        _jit->vector_quad_arguments(JIT_REGISTER_STATE,address,vector_offset);
        _jit->native_vector_call(get_quad_partial(store));
#endif
    };
    if(quad_native && !known) {
        auto *partial=_jit->bmsi(address,15);
        _jit->vector_memory(JIT_REGISTER_STATE,JIT_REGISTER_DMEM,address,vector_offset,store,size);
        auto *done=_jit->jmpi();_jit->patch(partial);
        partial_quad();
        _jit->patch(done);return true;
    }
    if(quad_native && known && ((base+offset)&15)) {
        partial_quad();return true;
    }
    if(packed)_jit->vector_packed_load(JIT_REGISTER_STATE,JIT_REGISTER_DMEM,address,vector_offset,type==6?8:7);
    else _jit->vector_memory(JIT_REGISTER_STATE,JIT_REGISTER_DMEM,address,vector_offset,store,size);
    if(unaligned) {
        auto *done=_jit->jmpi();_jit->patch(unaligned);
        jit_begin_call(_jit);_jit->pushargr(JIT_REGISTER_STATE);_jit->pushargi(vt);
        _jit->pushargi(element);_jit->pushargi(offset/int(size));_jit->pushargi(rs);
        using Operation=void(*)(CPUState*,unsigned,unsigned,int,unsigned);
        static const Operation loads[]={RSP_LBV,RSP_LSV,RSP_LLV,RSP_LDV,RSP_LQV};
        static const Operation stores[]={RSP_SBV,RSP_SSV,RSP_SLV,RSP_SDV,RSP_SQV};
        auto operation=packed?(type==6?RSP_LPV:RSP_LUV):(store?stores[type]:loads[type]);
        jit_end_call(_jit,reinterpret_cast<CodePointer>(operation));
        _jit->patch(done);
    }
    return true;
}

void CPU::jit_mark_block_entries(uint32_t pc, uint32_t end, bool *block_entries)
{
	unsigned count = end - pc;

	// Find all places where we need to insert a label.
	// This also affects codegen for static branches.
	// If the delay slot for a static branch is a block entry,
	// it is not actually a static branch, but a conditional one because
	// some other instruction might have branches into the delay slot.
	for (unsigned i = 0; i < count; i++)
	{
		uint32_t instr = state.imem[pc + i];
		uint32_t type = instr >> 26;
		uint32_t target;

		// VU
		if ((instr >> 25) == 0x25)
			continue;

		switch (type)
		{
		case 001: // REGIMM
			switch ((instr >> 16) & 31)
			{
			case 000: // BLTZ
			case 001: // BGEZ
			case 021: // BGEZAL
			case 020: // BLTZAL
				target = (pc + i + 1 + instr) & 0x3ff;
				if (target >= pc && target < end) // goto
					block_entries[target - pc] = true;
				break;

			default:
				break;
			}
			break;

		case 002:
		case 003:
			// J is resolved by goto. Same with JAL.
			target = instr & 0x3ff;
			if (target >= pc && target < end) // goto
				block_entries[target - pc] = true;
			break;

		case 004: // BEQ
		case 005: // BNE
		case 006: // BLEZ
		case 007: // BGTZ
			target = (pc + i + 1 + instr) & 0x3ff;
			if (target >= pc && target < end) // goto
				block_entries[target - pc] = true;
			break;

		default:
			break;
		}
	}
}

void CPU::jit_handle_latent_delay_slot(Emitter *_jit, const InstructionInfo &last_info)
{
	regs.flush_register_window(_jit);
	jit_branch_stall(_jit, false);
	unsigned cond_branch_reg = JIT_REGISTER_NEXT_PC;
	if (last_info.branch && last_info.conditional)
	{
		cond_branch_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	}
	regs.flush_register_window(_jit);

	if (last_info.branch)
	{
		// Well then ... two branches in a row just happened. Try to do something sensible.
		if (!last_info.conditional)
			_jit->movi(cond_branch_reg, 1);
		_jit->stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, cond_branch_reg);

		_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, branch_target));

		if (last_info.indirect)
			jit_load_indirect_register(_jit, JIT_REGISTER_MODE);
		else
			_jit->movi(JIT_REGISTER_MODE, last_info.branch_target);

		_jit->stxi_i(offsetof(CPUState, branch_target), JIT_REGISTER_STATE, JIT_REGISTER_MODE);
		_jit->jump_absolute(thunks.enter_thunk);
	}
	else
	{
		_jit->movi(JIT_REGISTER_NEXT_PC, 0);
		_jit->stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);
		_jit->ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, branch_target));
		_jit->jump_absolute(thunks.enter_thunk);
	}
}

// Only these instructions have no externally observable effects beyond their
// scalar destination. All other opcodes are barriers for backward liveness.
static bool scalar_access(uint32_t instr, unsigned &dest, uint32_t &reads)
{
    unsigned type = instr >> 26, op = instr & 63;
    unsigned rs = (instr >> 21) & 31, rt = (instr >> 16) & 31;
    if (type >= 8 && type <= 15) {
        dest = rt; reads = type == 15 ? 0 : 1u << rs; return true;
    }
    if (type != 0) return false;
    dest = (instr >> 11) & 31;
    switch (op) {
    case 0: case 2: case 3: reads = 1u << rt; return true;
    case 4: case 6: case 7:
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
    case 0x26: case 0x27: case 0x2a: case 0x2b: reads = (1u << rs) | (1u << rt); return true;
    default: return false;
    }
}

Func CPU::jit_region(uint64_t hash, unsigned pc_word, unsigned instruction_count)
{
	regs.reset();
#ifdef RSP_JIT_PROFILE
    ExecutionProfile *profile = nullptr;
    if (std::getenv("RSP_JIT_PROFILE_FILE")) {
        execution_profiles.emplace_back(new ExecutionProfile);
        profile = execution_profiles.back().get();
        profile->hash=hash;profile->pc=pc_word;profile->count=instruction_count;
        std::memcpy(profile->instructions,state.imem+pc_word,instruction_count*sizeof(uint32_t));
    }
#endif

	mips_disasm.clear();
	Emitter emitter;
	Emitter *_jit = &emitter;


	CodeLabel *branch_targets[CODE_BLOCK_WORDS * 2];
	CodeLabel *latent_delay_slot = nullptr;
	local_branches.clear();

	assert(instruction_count <= (CODE_BLOCK_WORDS * 2));

	// Mark which instructions can be branched to via local goto.
	bool block_entry[CODE_BLOCK_WORDS * 2];
	memset(block_entry, 0, instruction_count * sizeof(bool));
	jit_mark_block_entries(pc_word, pc_word + instruction_count, block_entry);

    bool dead_scalar[CODE_BLOCK_WORDS * 2] = {};
#ifndef TRACE
    uint32_t live_scalar = UINT32_MAX;
    for (unsigned end = instruction_count; end; ) {
        unsigned i = --end, dest; uint32_t reads;
        // Entry instruction may be a pending delay slot, and joins may have
        // predecessors outside the linear stream. Expose all registers there.
        if ((i + 1 < instruction_count && block_entry[i + 1]) ||
            (i && RSPTimingDecoder::decoderEXECUTE(state.imem[pc_word + i - 1]).branch()))
            live_scalar = UINT32_MAX;
        if (!scalar_access(state.imem[pc_word + i], dest, reads)) { live_scalar = UINT32_MAX; continue; }
        if (i && (!dest || !(live_scalar & (1u << dest)))) dead_scalar[i] = true;
        else live_scalar = (live_scalar & ~(1u << dest)) | reads;
    }
#endif

	InstructionInfo last_info = {};
	InstructionInfo first_info = {};
	RSPTimingPipeline timing = {};
	unsigned timing_cost[CODE_BLOCK_WORDS * 2] = {};
	for (unsigned i = 0; i < instruction_count; ++i) {
        if (!cycle_timing) { timing_cost[i] = 1; continue; }
		if (block_entry[i]) timing = {};
		auto op0 = RSPTimingDecoder::decoderEXECUTE(state.imem[pc_word + i]);
		bool pair = false;
		RSPTimingOp op1 = {};
		if (i + 1 < instruction_count && !block_entry[i + 1] && !timing.single &&
		    !op0.branch() && !op0.mayHalt() && !op0.endBlock()) {
			op1 = RSPTimingDecoder::decoderEXECUTE(state.imem[pc_word + i + 1]);
			pair = RSPTimingDecoder::canDualIssue(op0, op1);
		}
		timing_cost[i] = timing.issue(op0, pair ? &op1 : nullptr);
		if (pair) timing_cost[++i] = 0;
	}
	// Arithmetic, COP2 and DMEM helpers cannot observe the clock or exit the JIT.
	// Coalesce their counter updates, retaining exact costs at every externally
	// visible boundary. The first instruction may resolve a latent delay slot;
	// entries, branches and their delay slots must never carry a deferred cost.
	// DMEM is local memory even with detailed timing. Its decoded pipeline cost
	// can share updates with arithmetic; COP0/DMA and scheduler exits still flush.
#ifndef TRACE
	unsigned pending_cycles = 0;
	bool previous_branch = false;
	for (unsigned i = 0; i < instruction_count; ++i) {
		uint32_t instr = state.imem[pc_word + i];

		auto op = RSPTimingDecoder::decoderEXECUTE(instr);
		pending_cycles += timing_cost[i];
		bool flush = i == 0 || i + 1 == instruction_count || block_entry[i + 1] ||
		             previous_branch || op.branch() || op.mayHalt() || op.endBlock() ||
		             (instr >> 26) == 0x10;
		timing_cost[i] = flush ? pending_cycles : 0;
		if (flush) pending_cycles = 0;
		previous_branch = op.branch();
	}
#endif

	for (unsigned i = 0; i < instruction_count; i++)
	{
		if (block_entry[i] || (cycle_timing && i == 0))
		{
			// Before we enter into a new block, we have to flush register window since someone can branch here.
			regs.flush_register_window(_jit);
			regs.reset();
			branch_targets[i] = _jit->label();
			// A local target may also be a fallthrough delay slot. Resolve
			// its preceding branch before yielding; only region-entry delay
			// slots already have their pending branch in CPUState.
			if (cycle_timing && !last_info.branch) {
				_jit->ldxi_ui(JIT_REGISTER_MODE, JIT_REGISTER_STATE, offsetof(CPUState, cycles));
				_jit->ldxi_ui(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, cycle_limit));
				auto *continue_slice = _jit->bltr_u(JIT_REGISTER_MODE, JIT_REGISTER_NEXT_PC);
				_jit->movi(JIT_REGISTER_MODE, MODE_TIMESLICE);
				_jit->movi(JIT_REGISTER_NEXT_PC, (pc_word + i) << 2);
				_jit->jump_absolute(thunks.return_thunk);
				_jit->patch(continue_slice);
			}
		}

		uint32_t instr = state.imem[pc_word + i];

        unsigned vector_count = 1;
#ifndef TRACE
        // First instructions can resolve a latent delay slot; branch targets
        // and ordinary delay slots also require individually visible state.
        auto vector_only = [](uint32_t instruction) {
            unsigned op = instruction & 63;
            return (instruction >> 25) == 0x25 && !quantize_opcode(op) && (op < 0x30 || op > 0x36);
        };
        if (i && !last_info.branch && vector_only(instr))
            while (vector_count < 8 && i + vector_count < instruction_count &&
                   !block_entry[i + vector_count] && vector_only(state.imem[pc_word + i + vector_count]))
                ++vector_count;
#endif
        unsigned cycle_cost = 0;
        for (unsigned j = 0; j < vector_count; j++) cycle_cost += timing_cost[i + j];
		// Count execution, including loop iterations and branch delay slots.
		// Emit before instructions that can return to the task scheduler.
		// Omit zero-cost pair members and coalesced arithmetic updates, avoiding
		// both memory traffic and a needless scratch-register eviction.
		if (cycle_cost) {
			unsigned cycle_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			_jit->ldxi_ui(cycle_reg, JIT_REGISTER_STATE, offsetof(CPUState, cycles));
			_jit->addi(cycle_reg, cycle_reg, cycle_cost);
			_jit->stxi_i(offsetof(CPUState, cycles), JIT_REGISTER_STATE, cycle_reg);
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
		}

#ifdef TRACE_DISASM
		mips_disasm += disassemble((pc_word + i) << 2, instr);
		if (last_info.branch)
		{
			mips_disasm += "  [branch]";
			if (last_info.conditional)
				mips_disasm += "  [cond]";
			if (last_info.indirect)
				mips_disasm += "  [indirect]";
			if (last_info.handles_delay_slot)
				mips_disasm += "  [handles delay slot]";
		}
		if (block_entry[i])
			mips_disasm += "  [block entry]";
		mips_disasm += "\n";
#endif

		InstructionInfo inst_info = {};
        if (vector_count > 1) {
#ifdef RSP_JIT_PROFILE
            if(profile){profile->spans[i]=vector_count;_jit->profile_counter(profile->hits+i);}
#endif
            _jit->native_vector_call(get_vector_sequence(state.imem + pc_word + i, vector_count));
            i += vector_count - 1;
        } else {
#ifdef RSP_JIT_PROFILE
            if(profile){profile->spans[i]=1;_jit->profile_counter(profile->hits+i);}
#endif
		jit_instruction(_jit, (pc_word + i) << 2, dead_scalar[i] ? 0 : instr, inst_info, last_info, i == 0,
		                (i + 1 < instruction_count) && block_entry[i + 1]);
        }

		// Handle all the fun cases with branch delay slots.
		// Not sure if we really need to handle them, but IIRC CXD4 does it and the LLVM RSP as well.

		if (i == 0 && !inst_info.handles_delay_slot)
		{
			unsigned scratch_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			_jit->ldxi_i(scratch_reg, JIT_REGISTER_STATE, offsetof(CPUState, has_delay_slot));
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			regs.flush_register_window(_jit);

			// After the first instruction, we might need to resolve a latent delay slot.
			latent_delay_slot = _jit->bnei(scratch_reg, 0);
			first_info = inst_info;
		}
		else if (inst_info.branch && last_info.branch)
		{
			// "Impossible" handling of the delay slot.
			// Happens if we have two branch instructions in a row.
			// Weird magic happens here!
			jit_handle_impossible_delay_slot(_jit, inst_info, last_info, pc_word << 2, (pc_word + instruction_count) << 2);
		}
		else if (!inst_info.handles_delay_slot && last_info.branch)
		{
			// Normal handling of the delay slot.
			jit_handle_delay_slot(_jit, last_info, pc_word << 2, (pc_word + instruction_count) << 2);
		}
		last_info = inst_info;
	}

	regs.flush_register_window(_jit);

	// Jump to another block.
	jit_end_of_block(_jit, (pc_word + instruction_count) << 2, last_info);

	// If we had a latent delay slot, we handle it here.
	if (latent_delay_slot)
	{
		_jit->patch(latent_delay_slot);
		jit_handle_latent_delay_slot(_jit, first_info);
	}

	for (auto &b : local_branches)
		_jit->patch_at(b.node, branch_targets[b.local_index]);

	HostWord code_size;
	_jit->get_code(&code_size);
	auto *block_code = allocator.allocate_code(code_size);
	if (!block_code)
		abort();
	_jit->set_code(block_code, code_size);

	auto ret = reinterpret_cast<Func>(_jit->emit());
#ifdef RSP_JIT_CODE_DUMP
    if(const char *directory=std::getenv("RSP_JIT_CODE_DUMP")) {
        char path[1024];std::snprintf(path,sizeof(path),"%s/%03x.bin",directory,pc_word*4);
        if(core_file *file=core_file_open(path,"wb")) {
            core_file_write_items(block_code,size_t(code_size),1,file);core_file_close(file);
        }
    }
#endif

#ifdef TRACE_DISASM
	printf(" === DISASM ===\n");
	printf("%s\n", mips_disasm.c_str());
	printf(" === DISASM END ===\n\n");
#endif

	if (!Allocator::commit_code(block_code, code_size))
		abort();
	return ret;
}

ReturnMode CPU::run(uint32_t cycle_limit)
{
	state.cycle_limit = cycle_limit;
	invalidate_code();
	for (;;)
	{
		int ret = enter(state.pc);
		switch (ret)
		{
		case MODE_BREAK:
			*state.cp0.cr[CP0_REGISTER_SP_STATUS] |= SP_STATUS_BROKE | SP_STATUS_HALT;
			if (*state.cp0.cr[CP0_REGISTER_SP_STATUS] & SP_STATUS_INTR_BREAK)
				*state.cp0.irq |= 1;
#if !defined(PARALLEL_INTEGRATION) && !defined(PARALLAX_RSP_TEST_SILENT)
			print_registers();
#endif
			return MODE_BREAK;

		case MODE_CHECK_FLAGS:
		case MODE_TIMESLICE:
		case MODE_DMA_READ:
			return static_cast<ReturnMode>(ret);

		default:
			break;
		}
	}
}

void CPU::print_registers()
{
#define DUMP_FILE stdout
	fprintf(DUMP_FILE, "RSP state:\n");
	fprintf(DUMP_FILE, "  PC: 0x%03x\n", state.pc);
	for (unsigned i = 1; i < 32; i++)
		fprintf(DUMP_FILE, "  SR[%s] = 0x%08x\n", register_name(i), state.sr[i]);
	fprintf(DUMP_FILE, "\n");
	for (unsigned i = 0; i < 32; i++)
	{
		fprintf(DUMP_FILE, "  VR[%02u] = { 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x }\n", i,
		        state.cp2.regs[i].e[0], state.cp2.regs[i].e[1], state.cp2.regs[i].e[2], state.cp2.regs[i].e[3],
		        state.cp2.regs[i].e[4], state.cp2.regs[i].e[5], state.cp2.regs[i].e[6], state.cp2.regs[i].e[7]);
	}

	fprintf(DUMP_FILE, "\n");

	for (unsigned i = 0; i < 3; i++)
	{
		static const char *strings[] = { "ACC_HI", "ACC_MD", "ACC_LO" };
		fprintf(DUMP_FILE, "  %s = { 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x }\n", strings[i],
		        state.cp2.acc.e[8 * i + 0], state.cp2.acc.e[8 * i + 1], state.cp2.acc.e[8 * i + 2],
		        state.cp2.acc.e[8 * i + 3], state.cp2.acc.e[8 * i + 4], state.cp2.acc.e[8 * i + 5],
		        state.cp2.acc.e[8 * i + 6], state.cp2.acc.e[8 * i + 7]);
	}

	fprintf(DUMP_FILE, "\n");

	for (unsigned i = 0; i < 3; i++)
	{
		static const char *strings[] = { "VCO", "VCC", "VCE" };
		uint16_t flags = rsp_get_flags(state.cp2.flags[i].e);
		fprintf(DUMP_FILE, "  %s = 0x%04x\n", strings[i], flags);
	}

	fprintf(DUMP_FILE, "\n");
	fprintf(DUMP_FILE, "  Div Out = 0x%04x\n", state.cp2.div_out);
	fprintf(DUMP_FILE, "  Div In  = 0x%04x\n", state.cp2.div_in);
	fprintf(DUMP_FILE, "  DP flag = 0x%04x\n", state.cp2.dp_flag);
}

RegisterCache::CacheEntry *RegisterCache::find_live_mips_register(unsigned mips_reg)
{
	for (auto &entry : entries)
		if (entry.is_live && entry.mips_register == mips_reg)
			return &entry;
	return nullptr;
}

RegisterCache::CacheEntry *RegisterCache::find_free_register()
{
	for (auto &entry : entries)
		if (!entry.is_live)
			return &entry;
	return nullptr;
}

RegisterCache::CacheEntry *RegisterCache::find_oldest_unlocked_register()
{
	CacheEntry *best = nullptr;
	for (auto &entry : entries)
	{
		if (entry.is_live && !entry.num_locks)
		{
			if (!best || entry.timestamp < best->timestamp)
				best = &entry;
		}
	}
	return best;
}

RegisterCache::CacheEntry &RegisterCache::find_register(unsigned mips_reg)
{
	auto *reg = find_live_mips_register(mips_reg);
	if (!reg)
		reg = find_free_register();
	if (!reg)
		reg = find_oldest_unlocked_register();
	assert(reg);
	return *reg;
}

void RegisterCache::writeback_register(Emitter *_jit, CacheEntry &entry)
{
	// The scratch registers are never flushed out to memory.
	assert(entry.mips_register != 0);
	if (entry.mips_register <= COND_BRANCH_TAKEN)
		_jit->stxi_i(offsetof(CPUState, sr) + 4 * entry.mips_register, JIT_REGISTER_STATE, entry_to_jit_register(entry));
	entry.modified = false;
}

unsigned RegisterCache::immediate_mips_register(Emitter *_jit, unsigned mips_reg, HostWord value)
{
	unsigned jit_reg = modify_mips_register(_jit, mips_reg);
	_jit->movi(jit_reg, value);
	entries[jit_register_to_index(jit_reg)].sign = SExt;
    if (mips_reg < 32) { constant_known[mips_reg] = true; constants[mips_reg] = uint32_t(value); }
	return jit_reg;
}

unsigned RegisterCache::load_mips_register_noext(Emitter *_jit, unsigned mips_reg)
{
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);
	assert(mips_reg <= COND_BRANCH_TAKEN);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;

		if (mips_reg)
			_jit->ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			_jit->movi(jit_reg, 0);
		reg.modified = false;

		// We know that the input is sign-extended so future opcodes which rely on
		// sign-ness will be able to assume so.
		reg.sign = SExt;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;

		if (mips_reg)
			_jit->ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			_jit->movi(jit_reg, 0);

		reg.sign = SExt;
		reg.is_live = true;
		reg.modified = false;
	}

	// If the register is already live and well, we just need to update the timestamp.

	reg.timestamp = ++timestamp;
	reg.num_locks++;
	return jit_reg;
}

unsigned RegisterCache::modify_mips_register(Emitter *_jit, unsigned mips_reg)
{
    if (mips_reg < 32) constant_known[mips_reg] = false;
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;
		reg.is_live = true;
	}

	// If the register is already live and well, we just need to update the timestamp.

	reg.sign = Unknown;
	reg.timestamp = ++timestamp;
	reg.num_locks++;
	reg.modified = true;
	return jit_reg;
}

unsigned RegisterCache::load_mips_register_sext(Emitter *_jit, unsigned mips_reg)
{
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);
	assert(mips_reg <= COND_BRANCH_TAKEN);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;

		if (mips_reg)
			_jit->ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			_jit->movi(jit_reg, 0);

		reg.modified = false;
		reg.sign = SExt;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;

		if (mips_reg)
			_jit->ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			_jit->movi(jit_reg, 0);

		reg.sign = SExt;
		reg.is_live = true;
		reg.modified = false;
	}
	else if (reg.sign != SExt)
	{
#if INTPTR_MAX > INT32_MAX
		if (mips_reg)
		{
			// Have to sign-extend if we're not sure.
			_jit->extr_i(jit_reg, jit_reg);
		}
#endif
		reg.sign = SExt;
	}

	reg.num_locks++;
	reg.timestamp = ++timestamp;
	return jit_reg;
}

unsigned RegisterCache::load_mips_register_zext(Emitter *_jit, unsigned mips_reg)
{
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);
	assert(mips_reg <= COND_BRANCH_TAKEN);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;

		if (mips_reg)
			_jit->ldxi_ui(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			_jit->movi(jit_reg, 0);

		reg.modified = false;
		reg.sign = ZExt;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;

		if (mips_reg)
			_jit->ldxi_ui(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			_jit->movi(jit_reg, 0);

		reg.sign = ZExt;
		reg.is_live = true;
		reg.modified = false;
	}
	else if (reg.sign != ZExt)
	{
#if INTPTR_MAX > INT32_MAX
		if (mips_reg)
		{
			// Have to zero-extend if we're not sure.
			_jit->extr_ui(jit_reg, jit_reg);
		}
#endif
		reg.sign = ZExt;
	}

	reg.num_locks++;
	reg.timestamp = ++timestamp;
	return jit_reg;
}

void RegisterCache::unlock_mips_register(unsigned mips_reg)
{
	auto *live_reg = find_live_mips_register(mips_reg);
	assert(live_reg);
	assert(live_reg->num_locks > 0);
	live_reg->num_locks--;
}

void RegisterCache::flush_register_window(Emitter *_jit)
{
    std::memset(constant_known, 0, sizeof(constant_known));
	for (auto &entry : entries)
	{
		if (entry.is_live)
		{
			if (entry.modified)
				writeback_register(_jit, entry);
			assert(!entry.num_locks);
			entry = {};
		}
	}
	timestamp = 0;
}

void RegisterCache::flush_caller_save_registers(Emitter *_jit)
{
	for (unsigned i = 0; i < JIT_R_NUM; i++)
	{
		auto &entry = entries[jit_register_to_index(JIT_R(i))];
		if (entry.is_live)
		{
			if (entry.modified)
				writeback_register(_jit, entry);
			assert(!entry.num_locks);
			entry = {};
		}
	}
}

void RegisterCache::reset()
{
    std::memset(constant_known, 0, sizeof(constant_known));
	for (auto &entry : entries)
		entry = {};
}

void RegisterCache::flush_mips_register(Emitter *_jit, unsigned mips_reg)
{
    if (mips_reg < 32) constant_known[mips_reg] = false;
	auto *live_reg = find_live_mips_register(mips_reg);
	if (live_reg)
	{
		if (live_reg->modified)
			writeback_register(_jit, *live_reg);
		assert(!live_reg->num_locks);
		live_reg->is_live = false;
		*live_reg = {};
	}
}

unsigned RegisterCache::jit_register_to_index(unsigned jit_reg)
{
	if (jit_reg >= JIT_R0 && jit_reg < JIT_R(JIT_R_NUM))
		return jit_reg - JIT_R0;
	else
		return JIT_R_NUM + (jit_reg - JIT_V(3));
}

unsigned RegisterCache::entry_to_jit_register(const CacheEntry &entry)
{
	auto index = unsigned(&entry - entries);
	if (index < JIT_R_NUM)
		return JIT_R(index);
	else
		return JIT_V(3 + (index - JIT_R_NUM));
}

} // namespace JIT
} // namespace RSP
