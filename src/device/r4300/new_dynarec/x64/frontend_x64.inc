
#ifndef ARI64_X64_ARENA_BYTES
#define ARI64_X64_ARENA_BYTES (8 << TARGET_SIZE_2)
#endif
enum { CODE_BYTES = ARI64_X64_ARENA_BYTES, MAX_INSTRUCTIONS = 32 };
#ifndef ARI64_X64_SEGMENTS
/* Keep the default 8 MiB recycling granularity as the arena grows. */
#define ARI64_X64_SEGMENTS (CODE_BYTES >= (256 << 20) ? 32 : CODE_BYTES >= (1 << 20) ? 16 : 1)
#endif
enum { CODE_SEGMENTS = ARI64_X64_SEGMENTS,
    SEGMENT_BYTES = CODE_BYTES / CODE_SEGMENTS };
struct ari64 {
    unsigned char* code;
    size_t used, page_size;
    uint64_t generation, blocks, native;
    uint64_t emitted_bytes, relative_calls, absolute_calls;
    uint64_t segment_generation[CODE_SEGMENTS];
};

static int code_protect(void* ptr, size_t size, int executable)
{
#ifdef _WIN32
    DWORD previous;
    return VirtualProtect(ptr, size, executable ? PAGE_EXECUTE_READ : PAGE_READWRITE, &previous) != 0;
#else
    return mprotect(ptr, size, executable ? PROT_READ | PROT_EXEC : PROT_READ | PROT_WRITE) == 0;
#endif
}

#include "../code_write_scope.h"

struct ari64* ari64_create(void)
{
    struct ari64* s = (struct ari64*)calloc(1, sizeof(*s));
    if (!s) return NULL;
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    s->page_size = info.dwPageSize;
    /* Upstream linkage uses rel32 calls. Keep the arena within range of this
     * module, so precise helpers can use the same five-byte call encoding. */
    uintptr_t anchor = (uintptr_t)&ari64_create;
    uintptr_t cursor = anchor > UINT32_C(0x60000000) ? anchor - UINT32_C(0x60000000) : (uintptr_t)info.lpMinimumApplicationAddress;
    uintptr_t limit = anchor + UINT32_C(0x60000000);
    MEMORY_BASIC_INFORMATION region;
    while (cursor < limit && VirtualQuery((void*)cursor, &region, sizeof(region))) {
        uintptr_t end = (uintptr_t)region.BaseAddress + region.RegionSize;
        uintptr_t aligned = (cursor + info.dwAllocationGranularity - 1) & ~((uintptr_t)info.dwAllocationGranularity - 1);
        if (region.State == MEM_FREE && aligned < end && end - aligned >= CODE_BYTES && limit - cursor >= CODE_BYTES) {
            s->code = (unsigned char*)VirtualAlloc((void*)aligned, CODE_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (s->code) break;
        }
        if (end <= cursor) break;
        cursor = end;
    }
    if (!s->code) s->code = (unsigned char*)VirtualAlloc(NULL, CODE_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    s->page_size = (size_t)sysconf(_SC_PAGESIZE);
    s->code = (unsigned char*)mmap(NULL, CODE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s->code == MAP_FAILED) s->code = NULL;
#endif
    if (!s->code) { free(s); return NULL; }
    s->generation = 1;
    s->segment_generation[0] = 1;
    return s;
}

void ari64_destroy(struct ari64* s)
{
    if (!s) return;
#ifdef _WIN32
    VirtualFree(s->code, 0, MEM_RELEASE);
#else
    munmap(s->code, CODE_BYTES);
#endif
    free(s);
}

uint64_t ari64_generation(const struct ari64* s) { return s->generation; }
int ari64_entry_valid(const struct ari64* s, ari64_entry entry, uint64_t generation)
{
    uintptr_t offset = (uintptr_t)entry - (uintptr_t)s->code;
    return entry && offset < CODE_BYTES && s->segment_generation[offset / SEGMENT_BYTES] == generation;
}
const uint64_t* ari64_entry_generation_ptr(const struct ari64* s, ari64_entry entry)
{
    uintptr_t offset = (uintptr_t)entry - (uintptr_t)s->code;
    return entry && offset < CODE_BYTES ? &s->segment_generation[offset / SEGMENT_BYTES] : NULL;
}
void ari64_stats(const struct ari64* s, uint64_t* blocks, uint64_t* native)
{
    *blocks = s ? s->blocks : 0;
    *native = s ? s->native : 0;
}
void ari64_code_stats(const struct ari64* s, uint64_t stats[3])
{
    stats[0] = s ? s->emitted_bytes : 0;
    stats[1] = s ? s->relative_calls : 0;
    stats[2] = s ? s->absolute_calls : 0;
}

static void emit_mem(unsigned opcode, int wide, int reg, int base, int index, unsigned scale, size_t offset);

/* Every guest value lives in the existing r4300_core across helper calls.
 * R12 holds that base; RAX/RCX are disposable instruction temporaries. This
 * avoids assuming upstream's assembly hot-state offsets or spill layout. */
static void emit_state_load_raw(size_t offset, int reg)
{
    emit_mem(0x8b, 1, reg, R12, -1, 0, offset);
}
static void emit_state_store_raw(size_t offset, int reg)
{
    emit_mem(0x89, 1, reg, R12, -1, 0, offset);
}
static size_t gpr_offset(unsigned reg) { return offsetof(struct r4300_core, regs) + reg * 8; }
#include "regalloc_x64.inc"
static void emit_binary(unsigned opcode,int wide) { emit_reg_op(opcode,RCX,RAX,wide); }
static void emit_immediate(unsigned ext,uint32_t imm,int wide) { emit_imm_op(ext,RAX,imm,wide); }
static void emit_shift(unsigned kind,unsigned amount,int variable,int wide)
{
    if(variable) {
        if(wide==2) JIT(and_(Xbyak::Reg32(RCX),31));
        if(kind==4) JIT(shl(host_reg(RAX,wide),assembler->cl));
        else if(kind==5) JIT(shr(host_reg(RAX,wide),assembler->cl));
        else JIT(sar(host_reg(RAX,wide),assembler->cl));
    } else emit_shift_reg(RAX,kind,amount,wide);
}
static void emit_sign_extend_word() { JIT(movsxd(Xbyak::Reg64(RAX),Xbyak::Reg32(RAX))); }

static int native_opcode(enum r4300_opcode op)
{
    switch (op) {
    case R4300_OP_NOP: case R4300_OP_SYNC:
    case R4300_OP_ADDIU: case R4300_OP_DADDIU: case R4300_OP_ADDU: case R4300_OP_DADDU:
    case R4300_OP_SUBU: case R4300_OP_DSUBU: case R4300_OP_LUI:
    case R4300_OP_AND: case R4300_OP_ANDI: case R4300_OP_OR: case R4300_OP_ORI:
    case R4300_OP_XOR: case R4300_OP_XORI: case R4300_OP_NOR:
    case R4300_OP_SLT: case R4300_OP_SLTU: case R4300_OP_SLTI: case R4300_OP_SLTIU:
    case R4300_OP_SLL: case R4300_OP_SLLV: case R4300_OP_SRL: case R4300_OP_SRLV:
    case R4300_OP_SRA: case R4300_OP_SRAV:
    case R4300_OP_DSLL: case R4300_OP_DSLL32: case R4300_OP_DSLLV:
    case R4300_OP_DSRL: case R4300_OP_DSRL32: case R4300_OP_DSRLV:
    case R4300_OP_DSRA: case R4300_OP_DSRA32: case R4300_OP_DSRAV:
    case R4300_OP_MFHI: case R4300_OP_MFLO: case R4300_OP_MTHI: case R4300_OP_MTLO:
    case R4300_OP_MULT: case R4300_OP_MULTU: case R4300_OP_DMULT: case R4300_OP_DMULTU:
    case R4300_OP_DIV: case R4300_OP_DIVU: case R4300_OP_DDIV: case R4300_OP_DDIVU:
        return 1;
    default: return 0;
    }
}

static void emit_muldiv(uint32_t instruction, enum r4300_opcode op);
static void emit_native(uint32_t instruction, enum r4300_opcode op)
{
    unsigned rs = (instruction >> 21) & 31, rt = (instruction >> 16) & 31;
    unsigned rd = (instruction >> 11) & 31, sa = (instruction >> 6) & 31;
    unsigned destination = rd;
    int word = 0;
    if (op == R4300_OP_NOP || op == R4300_OP_SYNC) return;
    if ((instruction >> 26) == 0 && (instruction & 63) >= 24 && (instruction & 63) <= 31) {
        emit_muldiv(instruction, op); return;
    }
    if (op == R4300_OP_MTHI || op == R4300_OP_MTLO) {
        emit_state_load(gpr_offset(rs), RAX);
        emit_state_store(op == R4300_OP_MTHI ? offsetof(struct r4300_core, hi) : offsetof(struct r4300_core, lo), RAX);
        return;
    }
    switch (op) {
    case R4300_OP_ADDIU: case R4300_OP_DADDIU:
        destination = rt;
        emit_state_load(gpr_offset(rs), RAX);
        if (op == R4300_OP_ADDIU) { emit_addimm(RAX, (int16_t)instruction, RAX); word = 1; }
        else emit_addimm64(RAX, (int16_t)instruction, RAX);
        break;
    case R4300_OP_ANDI: case R4300_OP_ORI: case R4300_OP_XORI:
        destination = rt;
        emit_state_load(gpr_offset(rs), RAX);
        emit_immediate(op == R4300_OP_ANDI ? 4 : op == R4300_OP_ORI ? 1 : 6,
                       (uint16_t)instruction, op != R4300_OP_ANDI);
        break;
    case R4300_OP_LUI:
        destination = rt;
        emit_movimm((uint32_t)(uint16_t)instruction << 16, RAX); word = 1;
        break;
    case R4300_OP_SLTI: case R4300_OP_SLTIU:
        destination = rt;
        emit_state_load(gpr_offset(rs), RAX);
        emit_movimm64((int64_t)(int16_t)instruction, RCX);
        emit_binary(0x39, 1);
        emit_set_condition(RAX,op==R4300_OP_SLTI?12:2);
        break;
    case R4300_OP_MFHI: case R4300_OP_MFLO:
        emit_state_load(op == R4300_OP_MFHI ? offsetof(struct r4300_core, hi) : offsetof(struct r4300_core, lo), RAX);
        break;
    case R4300_OP_SLL: case R4300_OP_SLLV: case R4300_OP_SRL: case R4300_OP_SRLV:
    case R4300_OP_SRA: case R4300_OP_SRAV:
    case R4300_OP_DSLL: case R4300_OP_DSLL32: case R4300_OP_DSLLV:
    case R4300_OP_DSRL: case R4300_OP_DSRL32: case R4300_OP_DSRLV:
    case R4300_OP_DSRA: case R4300_OP_DSRA32: case R4300_OP_DSRAV: {
        int variable = op == R4300_OP_SLLV || op == R4300_OP_SRLV || op == R4300_OP_SRAV ||
                       op == R4300_OP_DSLLV || op == R4300_OP_DSRLV || op == R4300_OP_DSRAV;
        word = op == R4300_OP_SLL || op == R4300_OP_SLLV || op == R4300_OP_SRL ||
               op == R4300_OP_SRLV || op == R4300_OP_SRA || op == R4300_OP_SRAV;
        unsigned ext = op == R4300_OP_SLL || op == R4300_OP_SLLV || op == R4300_OP_DSLL ||
                       op == R4300_OP_DSLL32 || op == R4300_OP_DSLLV ? 4 :
                       op == R4300_OP_SRL || op == R4300_OP_SRLV || op == R4300_OP_DSRL ||
                       op == R4300_OP_DSRL32 || op == R4300_OP_DSRLV ? 5 : 7;
        if (op == R4300_OP_DSLL32 || op == R4300_OP_DSRL32 || op == R4300_OP_DSRA32) sa += 32;
        emit_state_load(gpr_offset(rt), RAX);
        if (variable) emit_state_load(gpr_offset(rs), RCX);
        emit_shift(ext, sa, variable, word ? (ext == 7 ? 2 : 0) : 1);
        break;
    }
    default:
        emit_state_load(gpr_offset(rs), RAX);
        emit_state_load(gpr_offset(rt), RCX);
        switch (op) {
        case R4300_OP_ADDU: case R4300_OP_DADDU:
            word = op == R4300_OP_ADDU; emit_binary(0x01, !word); break;
        case R4300_OP_SUBU: case R4300_OP_DSUBU:
            word = op == R4300_OP_SUBU; emit_binary(0x29, !word); break;
        case R4300_OP_AND: emit_binary(0x21, 1); break;
        case R4300_OP_OR: case R4300_OP_NOR:
            emit_binary(0x09, 1);
            if (op == R4300_OP_NOR) { JIT(not_(Xbyak::Reg64(RAX))); }
            break;
        case R4300_OP_XOR: emit_binary(0x31, 1); break;
        case R4300_OP_SLT: case R4300_OP_SLTU:
            emit_binary(0x39, 1);
            emit_set_condition(RAX,op==R4300_OP_SLT?12:2);
            break;
        default: abort();
        }
    }
    if (word) emit_sign_extend_word();
    if (destination) emit_state_store(gpr_offset(destination), RAX);
}

static uint64_t compile_relative_calls, compile_absolute_calls;
static void emit_helper_call(const void* function)
{
    const int64_t displacement=(int64_t)(uintptr_t)function-(int64_t)(uintptr_t)(out+5);
    if(displacement>=INT32_MIN && displacement<=INT32_MAX) { JIT(call(function)); ++compile_relative_calls; }
    else { emit_movimm64((intptr_t)function,R11); JIT(call(Xbyak::Reg64(R11))); ++compile_absolute_calls; }
}
static void emit_step_call(void* block, unsigned index, unsigned phase, ari64_step step)
{
    emit_mov64(R12, ARG1_REG);
    emit_movimm64((intptr_t)block, ARG2_REG);
    emit_movimm(index, ARG3_REG);
    emit_movimm(phase, ARG4_REG);
    emit_helper_call((const void*)step);
}


#include "memory_x64.inc"
#include "muldiv_x64.inc"
#include "alu_x64.inc"
#define OPT_TEMP0 RAX
#define OPT_TEMP1 RCX
#define OPT_TEMP2 RDX
#include "../optimizer_alu.inc"
#undef OPT_TEMP0
#undef OPT_TEMP1
#undef OPT_TEMP2
#include "branch_x64.inc"
#include "timing_x64.inc"
#include "fpu_x64.inc"

#include "link_x64.inc"
/* A failed RAM guard replays the branch and delay together. Checkpoint the
 * link register as well as fetch/hazard state before any branch side effects.
 * Cross-page delay slots retain the precise path. */
static bool native_ram_delay(uint32_t branch,uint32_t delay,uint32_t pc)
{
    if((pc&4095)==4092 ||
       !native_ram_opcode(r4300_get_idec(delay)->opcode))return false;
    unsigned major=branch>>26,sub=(branch>>16)&31;
    unsigned link=major==3 || (major==1 && sub>=16 && sub<=19)?31:
        major==0 && (branch&63)==9?(branch>>11)&31:0;
    /* Direct-code call handlers have cheap fetches. Their native checkpoint
     * and memory guards increased the instruction working set enough to lose
     * whole-core throughput. Mapped calls avoid the larger precise TLB path. */
    if(link && (pc&UINT32_C(0xc0000000))==UINT32_C(0x80000000))return false;
    return (major>=4 && major<=7) || (major>=20 && major<=23) || major==2 || major==3 ||
        (major==0 && ((branch&63)==8 || (branch&63)==9)) ||
        (major==1 && (sub<=3 || (sub>=16 && sub<=19)));
}
static void emit_ram_delay_checkpoint(uint32_t pc,uint32_t branch,bool restore)
{
    unsigned major=branch>>26,sub=(branch>>16)&31;
    unsigned link=major==3 || (major==1 && sub>=16 && sub<=19)?31:
        major==0 && (branch&63)==9?(branch>>11)&31:0;
    if(link) {
        emit_mem(0x8b,1,RAX,restore?RSP:R12,-1,0,restore?80:gpr_offset(link));
        emit_mem(0x89,1,RAX,restore?R12:RSP,-1,0,restore?gpr_offset(link):80);
    }
    static_assert(offsetof(struct r4300_core,pipeline_fpu_result_mask)==offsetof(struct r4300_core,pipeline_load_mask)+8 &&
        offsetof(struct r4300_core,pipeline_fpu_cc)==offsetof(struct r4300_core,pipeline_load_mask)+16 &&
        offsetof(struct r4300_core,pipeline_cached_store)==offsetof(struct r4300_core,pipeline_load_mask)+20,"contiguous pipeline checkpoint");
    for(unsigned i=0;i<3;i++) {
        size_t field=offsetof(struct r4300_core,pipeline_load_mask)+i*8;
        emit_mem(0x8b,1,RAX,restore?RSP:R12,-1,0,restore?24+i*8:field);
        emit_mem(0x89,1,RAX,restore?R12:RSP,-1,0,restore?field:24+i*8);
    }
    size_t tag=offsetof(struct r4300_core,icache_tags)+((pc>>5)&511)*4;
    emit_mem(0x8b,0,RAX,restore?RSP:R12,-1,0,restore?48:tag);
    emit_mem(0x89,0,RAX,restore?R12:RSP,-1,0,restore?tag:48);
    if((pc&UINT32_C(0xc0000000))!=UINT32_C(0x80000000)) {
        static_assert(offsetof(struct r4300_core,pipeline_itlb_map)==offsetof(struct r4300_core,pipeline_itlb_vpn)+8 &&
            offsetof(struct r4300_core,pipeline_itlb_lru)==offsetof(struct r4300_core,pipeline_itlb_vpn)+20,"contiguous instruction TLB checkpoint");
        for(unsigned i=0;i<3;i++) {
            size_t field=offsetof(struct r4300_core,pipeline_itlb_vpn)+i*8;
            emit_mem(0x8b,1,RAX,restore?RSP:R12,-1,0,restore?56+i*8:field);
            emit_mem(0x89,1,RAX,restore?R12:RSP,-1,0,restore?field:56+i*8);
        }
    }
}
int ari64_has_native(const uint32_t* ops, unsigned count, uint32_t delay_op, unsigned has_delay, unsigned idle)
{
    for (unsigned i = 0; i < count; ++i) {
        enum r4300_opcode op = r4300_get_idec(ops[i])->opcode;
        if (native_opcode(op) || native_ram_opcode(op) || native_fpu_transfer(op) || native_fpu_instruction(ops[i])) return 1;
    }
    return count && count < MAX_INSTRUCTIONS && has_delay && !idle &&
        native_branch_opcode(r4300_get_idec(ops[count - 1])->opcode) &&
        (native_opcode(r4300_get_idec(delay_op)->opcode) || native_ram_opcode(r4300_get_idec(delay_op)->opcode));
}
static unsigned char* emit_step(void* block, unsigned index, unsigned phase, ari64_step step)
{
    emit_step_call(block, index, phase, step);
    emit_reg_op(0x85,RAX,RAX,0);
    return emit_jump(4);
}

/* The prepare helper already charged a COP1 transfer's clocks and hazards.
 * The register-only transfer cannot invalidate code or change mappings.
 * Commit locally while no event is due; the original helper still handles
 * exact event boundaries and all preparation faults. */
static void emit_transfer_commit(void* block, unsigned index,
    const struct ari64_config* config, ari64_step step,
    unsigned char** exits, unsigned* exit_count)
{
    emit_mem_imm(0x83, 1, 7, R12, -1, 0, offsetof(struct r4300_core, cp0.cycle_count),0);
    unsigned char* event = emit_jump(13); /* signed >= 0 */
    emit_state_imm(offsetof(struct r4300_core, interp_PC.addr), config->virtual_pc + (index + 1) * 4, 0);
    emit_state_imm(gpr_offset(0), 0, 1);
    if (config->commits) {
        emit_movimm64((intptr_t)config->commits, R11);
        emit_mem_imm(0x83, 1, 0, R11, -1, 0, 0,1);
    }
    emit_movimm64((intptr_t)&breakloop, R11);
    emit_mem_imm(0x80, 0, 7, R11, -1, 0, 0,0);
    exits[(*exit_count)++] = emit_jump(5);
    unsigned char* done = emit_b();
    patch_jump(event, out);
    exits[(*exit_count)++] = emit_step(block, index, 1, step);
    patch_jump(done, out);
}

#include "../fpu_sequence.inc"
static ari64_entry compile_block(struct ari64* s, void* block,
    const uint32_t* ops, unsigned count, ari64_step step, int optimized, const struct ari64_config* config)
{
    ari64_link link = config ? config->link : NULL;
    if (!s || !ops || !step || !count || count > MAX_INSTRUCTIONS) return NULL;
    uint32_t extended_ops[MAX_INSTRUCTIONS];
    unsigned branch_index = MAX_INSTRUCTIONS;
    bool ram_delay=false;
    if (optimized && config && config->has_delay && !config->idle_end && count < MAX_INSTRUCTIONS &&
        native_branch_opcode(r4300_get_idec(ops[count - 1])->opcode) &&
        (native_opcode(r4300_get_idec(config->delay_op)->opcode) ||
         (ram_delay=native_ram_delay(ops[count-1],config->delay_op,config->virtual_pc+(count-1)*4)))) {
        memcpy(extended_ops, ops, count * sizeof(*ops));
        branch_index = count - 1; extended_ops[count++] = config->delay_op; ops = extended_ops;
    }
    /* Native RAM guards have a snapshot writeback stub at every fault site.
     * Mapped fetch-cache probes occur in both the entry and resume guards;
     * account for them even in a one-instruction block with direct links.
     * Compile only while no generated frame is active. */
    size_t reserve = (count * 2048 + 512 + 15) & ~(size_t)15;
    if (optimized && config && config->cache_timing && config->fetch_cache &&
        config->fetch_generation && (config->virtual_pc & UINT32_C(0xc0000000)) != UINT32_C(0x80000000))
        reserve += 2048 + count * 1024;
    if (reserve > SEGMENT_BYTES) return NULL;
    if ((s->used % SEGMENT_BYTES) + reserve > SEGMENT_BYTES) {
        s->used = ((s->used / SEGMENT_BYTES + 1) * SEGMENT_BYTES) % CODE_BYTES;
        if (!code_protect(s->code + s->used, SEGMENT_BYTES, 0)) return NULL;
        s->segment_generation[s->used / SEGMENT_BYTES] = ++s->generation;
    }
    unsigned char* start = s->code + s->used;
    size_t page_start = s->used & ~(s->page_size - 1);
    size_t page_end = (s->used + reserve + s->page_size - 1) & ~(s->page_size - 1);
    /* Pack aligned entries into pages. Publication may share the preceding
     * block's page; compilation only runs with every generated frame unwound,
     * so temporarily making that page writable cannot interrupt native code. */
    if (!code_protect(s->code + page_start, page_end - page_start, 0)) return NULL;
    code_write_scope publication(s->code+page_start,page_end-page_start);
    unsigned char* exits[MAX_INSTRUCTIONS * 14 + 16];
    struct memory_exit memory_exits[MAX_INSTRUCTIONS];
    unsigned memory_exit_count = 0;
    unsigned exit_count = 0;
    x64_emitter code(reserve,start);
    emitter_binding binding(code);
    out=start;
    compile_relative_calls = compile_absolute_calls = 0;
    /* ENDBR64 is a NOP on CPUs without CET. */
    JIT(endbr64());
    JIT(push(Xbyak::Reg64(R12))); /* push R12; 16-byte call alignment */
    if (optimized) {
        JIT(push(Xbyak::Reg64(RBX))); JIT(push(Xbyak::Reg64(RBP))); /* RBX/RBP register cache */
#ifdef _WIN32
        JIT(push(Xbyak::Reg64(RSI))); JIT(push(Xbyak::Reg64(RDI))); /* RSI/RDI register cache */
#endif
        JIT(push(Xbyak::Reg64(R13))); /* R13 pending clocks */
        JIT(push(Xbyak::Reg64(R14))); /* R14 memory pointer */
        JIT(push(Xbyak::Reg64(R15))); /* R15 register cache */
    }
    emit_mov64(ARG1_REG, R12);
#ifdef _WIN32
    emit_addimm64(RSP, optimized ? -88 : -32, RSP); /* shadow space, FPU scratch, alignment */
#else
    if (optimized) emit_addimm64(RSP, -88, RSP);
#endif
#ifndef ARI64_X64_CHAIN_ALIGNMENT
#define ARI64_X64_CHAIN_ALIGNMENT 32
#endif
    static_assert(ARI64_X64_CHAIN_ALIGNMENT && !(ARI64_X64_CHAIN_ALIGNMENT&(ARI64_X64_CHAIN_ALIGNMENT-1)) && ARI64_X64_CHAIN_ALIGNMENT<=64,"bounded power-of-two native entry alignment");
#if ARI64_X64_CHAIN_ALIGNMENT > 1
    // Tail links land here directly, skipping the dispatcher-only prologue.
    JIT(align(ARI64_X64_CHAIN_ALIGNMENT));
#endif
    unsigned char* chain_entry = out;
    JIT(endbr64());
    for (unsigned i = 0; i < count;) {
        enum r4300_opcode op = r4300_get_idec(ops[i])->opcode;
        if (native_opcode(op) || i == branch_index || (optimized && config && native_ram_opcode(op))) {
            unsigned end = i + 1;
            if (optimized) {
                int memory_region = native_ram_opcode(op);
                while (end < count) {
                    if(ram_delay && end==branch_index)break;
                    enum r4300_opcode next = r4300_get_idec(ops[end])->opcode;
                    if (!native_opcode(next) && end != branch_index && !(config && native_ram_opcode(next))) break;
                    memory_region |= native_ram_opcode(next); ++end;
                }
                unsigned base = 0, memory_count = 0;
                for (unsigned j = i; j < end; ++j) {
                    base += r4300_base_cycles(ops[j], 0);
                    memory_count += native_ram_opcode(r4300_get_idec(ops[j])->opcode) != 0;
                }
                int branch_region = branch_index >= i && branch_index < end;
                unsigned branch_major = branch_region ? ops[branch_index] >> 26 : 0;
                int likely_region = branch_region && ((branch_major >= 20 && branch_major <= 23) ||
                    (branch_major == 1 && ((ops[branch_index] >> 16) & 2)));
                unsigned char* annul = NULL;
                struct regcache_state annul_registers;
                struct memory_exit prepare = {0};
                emit_region_guard(config, i, end - i, base, memory_count, &prepare);
                regcache_begin(ops, i, end);
                if(ram_delay && i==branch_index)emit_ram_delay_checkpoint(config->virtual_pc+i*4,ops[branch_index],false);
                emit_mem_imm(0xc7, 0, 0, RSP, -1, 0, 8,0);
                if (memory_region || likely_region) regcache.allow_dead_store = 0;
                emit_movimm(0, R13);
                int pipeline_live = 1;
                unsigned pending_cycles=0,pending_fetches=0;
                for (unsigned j = i; j < end; ++j) {
                    regcache.index = j;
                    /* Native RAM does not alter I-cache tags. A precise RAM
                     * continuation has already fetched the current line;
                     * only the next line needs another tag check. */
                    int check_line = j == i || ((config->virtual_pc + j * 4) >> 5) !=
                        ((config->virtual_pc + (j - 1) * 4) >> 5);
                    enum r4300_opcode instruction_op = r4300_get_idec(ops[j])->opcode;
                    if (native_ram_opcode(instruction_op)) {
                        if(pending_cycles||pending_fetches){emit_pending_clocks(config,pending_cycles,pending_fetches);pending_cycles=pending_fetches=0;}
                        struct memory_exit* exit = &memory_exits[memory_exit_count++];
                        exit->start = i; exit->index = j; exit->end = end; exit->guard_count = 0;
                        emit_memory_native(ops[j], j, config, exit, check_line,
                            j == i ? 0 : ops[j - 1], j != i);
                        pipeline_live = 1;
                        exit->resume = out; exit->resume_registers = regcache;
                    } else {
                        pending_cycles+=r4300_base_cycles(ops[j],0)+r4300_issue_reservation(config->compat_issue_clock);
                        ++pending_fetches;
                        emit_fetch_native(config, j, i, r4300_base_cycles(ops[j], 0), check_line,true);
                        unsigned reservation = r4300_issue_reservation(config->compat_issue_clock);
                        if (config->cache_timing && !reservation && pipeline_live) {
                            emit_ldi_native(ops[j], 0, j == i ? 0 : ops[j - 1], j != i);
                            emit_pipeline_clear();
                            pipeline_live = 0;
                        }
                        if (j == branch_index) {
                            if(pending_cycles||pending_fetches){emit_pending_clocks(config,pending_cycles,pending_fetches);pending_cycles=pending_fetches=0;}
                            annul = emit_branch_native(ops[j], config->virtual_pc + j * 4);
                            annul_registers = regcache;
                            if(ram_delay)emit_mem(0x89,0,R14,RSP,-1,0,16);
                        }
                        else if (!regcache_optimize(ops[j])) emit_native(ops[j], instruction_op);
                        if ((!config->cache_timing || reservation) &&
                            (j + 1 == end || native_ram_opcode(r4300_get_idec(ops[j + 1])->opcode))) emit_pipeline_clear();
                    }
                }
                emit_pending_clocks(config,pending_cycles,pending_fetches);
                if(ram_delay && branch_region)emit_mem(0x8b,0,R14,RSP,-1,0,16);
                regcache_flush();
                emit_region_commit_native(config, i, end - i, branch_region);
                if (annul) {
                    unsigned char* taken = emit_b();
                    patch_jump(annul, out);
                    regcache = annul_registers; regcache_flush();
                    emit_pipeline_clear();
                    emit_region_commit_native(config, i, end - i - 1, 1);
                    patch_jump(taken, out);
                }
                /* Unconditional jump over the precise event/mode slow path. */
                unsigned char* done = emit_b();
                for (unsigned g = 0; g < prepare.guard_count; ++g) patch_jump(prepare.guards[g], out);
                exits[exit_count++] = emit_step(block, i, ((ram_delay && branch_region ? 1 : end-i) << 8) | 5, step);
                patch_jump(done, out);
            } else {
                exits[exit_count++] = emit_step(block, i, 0, step);
                emit_native(ops[i], op);
                exits[exit_count++] = emit_step(block, i, 1, step);
            }
            s->native += end - i;
            i = end;
        } else if (optimized && native_fpu_transfer(op)) {
            exits[exit_count++] = emit_step(block,i,11,step);
            emit_fpu_transfer(ops[i],op);
            emit_transfer_commit(block,i,config,step,exits,&exit_count);
            ++s->native; ++i;
        } else if (optimized && config->fpu_prepare && config->fpu_finish && native_fpu_instruction(ops[i])) {
            unsigned end=emit_fpu_sequence(block,ops,i,count,config,step,exits,&exit_count);
            s->native+=end-i;i=end;
        } else { exits[exit_count++] = emit_step(block, i, 2, step); ++i; }
    }
    if (memory_exit_count) {
        exits[exit_count++] = emit_b();
        for (unsigned n = 0; n < memory_exit_count; ++n) {
            struct memory_exit* exit = &memory_exits[n];
            for (unsigned g = 0; g < exit->guard_count; ++g) patch_jump(exit->guards[g], out);
            if(ram_delay && exit->index==branch_index+1) {
                /* Restore the link register and branch fetch/hazard state,
                 * then execute the branch and delay together precisely. */
                emit_ram_delay_checkpoint(config->virtual_pc+branch_index*4,ops[branch_index],true);
                exits[exit_count++]=emit_step(block,branch_index,2,step);
                exits[exit_count++]=emit_b();
                continue;
            }
            regcache = exit->registers;
            regcache_flush();
            emit_region_commit_native(config, exit->start, exit->index - exit->start, 0);
            exits[exit_count++] = emit_step(block, exit->index, 2, step);
            unsigned base = 0, memory_count = 0;
            for (unsigned j = exit->index + 1; j < exit->end; ++j) {
                base += r4300_base_cycles(ops[j], 0);
                memory_count += native_ram_opcode(r4300_get_idec(ops[j])->opcode) != 0;
            }
            struct memory_exit prepare = {0};
            emit_region_guard(config, exit->index + 1, exit->end - exit->index - 1, base, memory_count, &prepare);
            for (unsigned g = 0; g < prepare.guard_count; ++g) exits[exit_count++] = prepare.guards[g];
            emit_mem_imm(0xc7, 0, 0, RSP, -1, 0, 8,exit->index + 1 - exit->start);
            emit_movimm(0, R13);
            /* Helpers may clobber volatile host registers and the loaded
             * guest destination. Restore the post-instruction allocation. */
            for (unsigned h = 0; h < CACHE_REGS; ++h)
                if (exit->resume_registers.map[h] >= 0)
                    emit_state_load_raw(guest_offset(exit->resume_registers.map[h]), cache_host[h]);
            unsigned char* resume = emit_b();
            patch_jump(resume, exit->resume);
        }
    }
    unsigned char* epilogue = out;
    if (link) {
        if (config->links) emit_direct_links(block,config);
        emit_mov64(R12, ARG1_REG);
        emit_helper_call((const void*)link);
        emit_reg_op(0x85,RAX,RAX,1);
        unsigned char* no_link=emit_jump(4); JIT(jmp(Xbyak::Reg64(RAX))); patch_jump(no_link,out);
    }
#ifdef _WIN32
    emit_addimm64(RSP, optimized ? 88 : 32, RSP);
#else
    if (optimized) emit_addimm64(RSP, 88, RSP);
#endif
    if (optimized) {
        JIT(pop(Xbyak::Reg64(R15)));
        JIT(pop(Xbyak::Reg64(R14)));
        JIT(pop(Xbyak::Reg64(R13)));
#ifdef _WIN32
        JIT(pop(Xbyak::Reg64(RDI))); JIT(pop(Xbyak::Reg64(RSI)));
#endif
        JIT(pop(Xbyak::Reg64(RBP))); JIT(pop(Xbyak::Reg64(RBX)));
    }
    JIT(pop(Xbyak::Reg64(R12)));

    JIT(ret());
    for (unsigned i = 0; i < exit_count; ++i) {
        patch_jump(exits[i], epilogue);
    }
    code.ready();
    assert((size_t)(out - start) <= reserve);
    s->used = ((size_t)(out - s->code) + 15) & ~(size_t)15;
    page_end = (s->used + s->page_size - 1) & ~(s->page_size - 1);
    if (!code_protect(s->code + page_start, page_end - page_start, 1)) return NULL;
    publication.published=true;
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), start, (size_t)(out - start));
#else
    __builtin___clear_cache((char*)start, (char*)out);
#endif
    ++s->blocks;
    s->emitted_bytes += out - start;
    s->relative_calls += compile_relative_calls;
    s->absolute_calls += compile_absolute_calls;
    if (config && config->chain_entry) *config->chain_entry = (ari64_entry)(void*)chain_entry;
    return (ari64_entry)(void*)start;
}

ari64_entry ari64_compile(struct ari64* s, void* block,
    const uint32_t* ops, unsigned count, ari64_step step)
{
    try { return compile_block(s, block, ops, count, step, 0, NULL); }
    catch(const std::exception& error) { core_log(CORE_LOG_ERROR,"Xbyak emission failed (%u instructions): %s",count,error.what()); return NULL; }
}
ari64_entry ari64_compile_optimized(struct ari64* s, void* block,
    const uint32_t* ops, unsigned count, ari64_step step, const struct ari64_config* config)
{
    try { return compile_block(s, block, ops, count, step, 1, config); }
    catch(const std::exception& error) { core_log(CORE_LOG_ERROR,"Xbyak emission failed at %08x (%u instructions): %s",config?config->virtual_pc:0,count,error.what()); return NULL; }
}
