
#ifndef N64EMU_DEVICE_R4300_FPU_NEON_H
#define N64EMU_DEVICE_R4300_FPU_NEON_H

#if defined(__aarch64__) && defined(__ARM_NEON) && \
    (defined(__GNUC__) || defined(__clang__)) && !defined(N64EMU_FPU_PORTABLE)
#define FPU_NATIVE_NEON 1

static FPU_NOINLINE uint64_t fpu_neon_calc(unsigned int fmt, unsigned int fn,
    uint64_t a, uint64_t b)
{
    uint64_t result;
    if ((fn >= 8 && fn <= 15) || fn == 36 || fn == 37) {
        /* FRINTX obeys FPCR and reports inexact. FCVTZS then converts the exact
         * integral value. Input limits were validated by the guest layer. */
        if (fmt == 16) {
            __asm__ volatile("fmov s0, %w1\n\tfrintx s0, s0\n\tfcvtzs %0, s0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        } else {
            __asm__ volatile("fmov d0, %1\n\tfrintx d0, d0\n\tfcvtzs %0, d0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        }
        return result;
    }
    if (fn == 32) {
        if (fmt == 17) {
            __asm__ volatile("fmov d0, %1\n\tfcvt s0, d0\n\tfmov %w0, s0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        } else if (fmt == 20) {
            __asm__ volatile("scvtf s0, %w1\n\tfmov %w0, s0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        } else {
            __asm__ volatile("scvtf s0, %1\n\tfmov %w0, s0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        }
        return result;
    }
    if (fn == 33) {
        if (fmt == 16) {
            __asm__ volatile("fmov s0, %w1\n\tfcvt d0, s0\n\tfmov %0, d0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        } else if (fmt == 20) {
            __asm__ volatile("scvtf d0, %w1\n\tfmov %0, d0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        } else {
            __asm__ volatile("scvtf d0, %1\n\tfmov %0, d0"
                : "=r"(result) : "r"(a) : "v0", "memory");
        }
        return result;
    }

#define NEON_BINARY_S(instruction) \
    __asm__ volatile("fmov s0, %w1\n\tfmov s1, %w2\n\t" instruction " s0, s0, s1\n\tfmov %w0, s0" \
        : "=r"(result) : "r"(a), "r"(b) : "v0", "v1", "memory")
#define NEON_BINARY_D(instruction) \
    __asm__ volatile("fmov d0, %1\n\tfmov d1, %2\n\t" instruction " d0, d0, d1\n\tfmov %0, d0" \
        : "=r"(result) : "r"(a), "r"(b) : "v0", "v1", "memory")
    if (fmt == 16) {
        switch (fn) {
        case 0: NEON_BINARY_S("fadd"); break;
        case 1: NEON_BINARY_S("fsub"); break;
        case 2: NEON_BINARY_S("fmul"); break;
        case 3: NEON_BINARY_S("fdiv"); break;
        default:
            __asm__ volatile("fmov s0, %w1\n\tfsqrt s0, s0\n\tfmov %w0, s0"
                : "=r"(result) : "r"(a) : "v0", "memory");
            break;
        }
    } else {
        switch (fn) {
        case 0: NEON_BINARY_D("fadd"); break;
        case 1: NEON_BINARY_D("fsub"); break;
        case 2: NEON_BINARY_D("fmul"); break;
        case 3: NEON_BINARY_D("fdiv"); break;
        default:
            __asm__ volatile("fmov d0, %1\n\tfsqrt d0, d0\n\tfmov %0, d0"
                : "=r"(result) : "r"(a) : "v0", "memory");
            break;
        }
    }
#undef NEON_BINARY_S
#undef NEON_BINARY_D
    return result;
}

static unsigned int fpu_neon_eval(unsigned int fmt, unsigned int fn,
    uint64_t a, uint64_t b, unsigned int rm, uint64_t* result)
{
    /* MIPS RN/RZ/RP/RM -> ARM RN/RZ/RP/RM (00/11/01/10). Clearing
     * the remaining FPCR fields disables flushing, default NaNs and traps. */
    static const unsigned int modes[4] = {0, 3, 1, 2};
    uint64_t control = (uint64_t)modes[rm] << 22;
    uint64_t saved_control, saved_status, status;
    __asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr\n\tmsr fpcr, %2\n\tmsr fpsr, xzr"
        : "=&r"(saved_control), "=&r"(saved_status) : "r"(control) : "memory");
    *result = fpu_neon_calc(fmt, fn, a, b);
    __asm__ volatile("mrs %0, fpsr\n\tmsr fpcr, %1\n\tmsr fpsr, %2"
        : "=&r"(status) : "r"(saved_control), "r"(saved_status) : "memory");
    if ((fn >= 12 && fn <= 15) || fn == 36) {
        int64_t value = (int64_t)*result;
        if (value > INT32_MAX || value < INT32_MIN) return 32;
        *result = (uint32_t)value;
    }
    /* FPSR IXC/UFC/OFC/DZC/IOC -> guest I/U/O/Z/V. */
    return ((status >> 4) & 1) | ((status >> 2) & 2) | (status & 4) |
        ((status << 2) & 8) | ((status << 4) & 16);
}
#elif defined(__arm__) && defined(__ARM_FP) && (__ARM_FP & 8) && \
    (defined(__GNUC__) || defined(__clang__)) && !defined(N64EMU_FPU_PORTABLE)
#define FPU_NATIVE_VFP 1

/* Execute the scalar operation between explicit FPSCR writes so the compiler
 * and CRT cannot change its rounding or handle exceptional results differently. */
static inline unsigned int fpu_vfp_arithmetic(unsigned fmt, unsigned fn,
    uint64_t a, uint64_t b, unsigned rm, uint64_t* bits)
{
    static const unsigned modes[4] = {0, 3, 1, 2};
    const unsigned control = modes[rm] << 22;
    unsigned saved, status;
    uint32_t single;
    uint64_t wide;
#define VFP_TRANSACTION(load, arithmetic, store, result) \
    __asm__ volatile( \
        "vmrs %[saved], fpscr\n\tvmsr fpscr, %[control]\n\t" \
        load "\n\t" arithmetic "\n\t" store "\n\t" \
        "vmrs %[status], fpscr\n\tvmsr fpscr, %[saved]" \
        : [saved] "=&r" (saved), [status] "=&r" (status) \
        : [control] "r" (control), [left] "r" (&a), [right] "r" (&b), [out] "r" (&result) \
        : "d0", "d1", "memory")
#define VFP_BINARY(name) do { \
    if (fmt == 16) \
        VFP_TRANSACTION("vldr s0, [%[left]]\n\tvldr s2, [%[right]]", name ".f32 s0, s0, s2", \
            "vstr s0, [%[out]]", single); \
    else \
        VFP_TRANSACTION("vldr d0, [%[left]]\n\tvldr d1, [%[right]]", name ".f64 d0, d0, d1", \
            "vstr d0, [%[out]]", wide); \
} while (0)
    switch (fn) {
    case 0: VFP_BINARY("vadd"); break;
    case 1: VFP_BINARY("vsub"); break;
    case 2: VFP_BINARY("vmul"); break;
    case 3: VFP_BINARY("vdiv"); break;
    case 4:
        if (fmt == 16) VFP_TRANSACTION("vldr s0, [%[left]]","vsqrt.f32 s0, s0","vstr s0, [%[out]]",single);
        else VFP_TRANSACTION("vldr d0, [%[left]]","vsqrt.f64 d0, d0","vstr d0, [%[out]]",wide);
        break;
    case 32:
        VFP_TRANSACTION("vldr d0, [%[left]]", "vcvt.f32.f64 s0, d0", "vstr s0, [%[out]]", single);
        break;
    default: /* CVT.D.S */
        VFP_TRANSACTION("vldr s0, [%[left]]", "vcvt.f64.f32 d0, s0", "vstr d0, [%[out]]", wide);
        break;
    }
#undef VFP_BINARY
#undef VFP_TRANSACTION
    *bits = fn == 32 || (fn <= 4 && fmt == 16) ? single : wide;
    return ((status >> 4) & 1) | ((status >> 2) & 2) | (status & 4) |
           ((status << 2) & 8) | ((status << 4) & 16);
}
#endif
#endif
