/* Replay Thar0's hardware-captured initial/final memory images. */
#define main archived_cpu_timing_main
#include "../cpu_timing.c"
#undef main
#include <limits.h>

static uint32_t *ram_words;
static const unsigned ram_bytes = 8 * 1024 * 1024;
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void image_read(void *opaque, uint32_t address, uint32_t *value)
{
    (void)opaque;
    *value = address < ram_bytes ? ram_words[address >> 2] : 0;
}
static void image_write(void *opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    (void)opaque;
    if (address < ram_bytes) masked_write(&ram_words[address >> 2], value, mask);
}
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    ram_words = calloc(1, ram_bytes);
    if (!ram_words) return 2;
    unsigned passed = 0, failed = 0;
    for (int file_index = 1; file_index < argc; ++file_index) {
        FILE *file = fopen(argv[file_index], "rb");
        if (!file) { perror(argv[file_index]); return 2; }
        fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
        unsigned char *data = malloc(length);
        if (!data || fread(data, 1, length, file) != (size_t)length) return 2;
        fclose(file);
        if (length < 32) return 2;
        uint32_t code_offset=be32(data), code_size=be32(data+4);
        uint32_t input=be32(data+8), expected=be32(data+12), size=be32(data+16);
        uint32_t code_va=be32(data+20), data_va=be32(data+24);
        uint32_t code_pa=code_va&0x1fffffff, data_pa=data_va&0x1fffffff;
        if ((code_size|size|code_pa|data_pa)&3 || code_offset+(uint64_t)code_size>(uint64_t)length ||
            input+(uint64_t)size>(uint64_t)length || expected+(uint64_t)size>(uint64_t)length ||
            code_pa+(uint64_t)code_size>ram_bytes || data_pa+(uint64_t)size>ram_bytes) return 2;
        setup(0);
        memset(ram_words, 0, ram_bytes);
        for (unsigned i=0;i<ram_bytes/65536;++i) {
            memory.handlers[i].read32=image_read;
            memory.handlers[i].write32=image_write;
        }
        memory.base=ram_words;
        for (unsigned i=0;i<code_size;i+=4) ram_words[(code_pa+i)/4]=be32(data+code_offset+i);
        for (unsigned i=0;i<size;i+=4) ram_words[(data_pa+i)/4]=be32(data+input+i);
        cpu.cache_timing=1; cpu.compat_issue_clock=0;
        cpu.cp0.regs[CP0_CONFIG_REG]=3;
        cpu.cp0.cycle_count=-INT64_C(10000000000);
        cpu.regs[31]=(int64_t)(int32_t)UINT32_C(0x80001000);
        cpu.regs[29]=(int64_t)(int32_t)UINT32_C(0x805ff000);
        generic_jump_to(&cpu, code_va);
        cpu.execute_one=r4300_jit_execute_one;
        if (!r4300_jit_init(&cpu)) return 2;
        while (cpu.instruction_pc.addr!=UINT32_C(0x80001000) && cpu.retired_ops<10000000 && !cpu.stop)
            r4300_jit_execute_one(&cpu);
        if (cpu.instruction_pc.addr!=UINT32_C(0x80001000)) {
            fprintf(stderr,"%s did not return: PC=%08x ops=%llu\n",argv[file_index],cpu.instruction_pc.addr,
                (unsigned long long)cpu.retired_ops);
            free(data);++failed;continue;
        }
        int mismatch = 0;
        for (unsigned i=0;i<size;i+=4) {
            unsigned index=((data_va+i)>>4)&511;
            uint32_t key=((data_pa+i)&0x1ffff000)|1;
            uint32_t actual=(cpu.dcache_tags[index]&~2u)==key ?
                cpu.dcache_words[index][((data_pa+i)>>2)&3] : ram_words[(data_pa+i)/4];
            uint32_t want=be32(data+expected+i);
            if (actual!=want) {
                fprintf(stderr,"%s offset=%x actual=%08x expected=%08x\n",argv[file_index],i,actual,want);
                mismatch = 1;break;
            }
        }
        if (mismatch) { free(data);++failed;continue; }
        printf("PASS %s (%u bytes, %llu guest instructions)\n",argv[file_index],size,
            (unsigned long long)cpu.retired_ops); fflush(stdout);
        free(data);++passed;
    }
    r4300_jit_destroy(&cpu);free(ram_words);
    printf("Thar0: %u passed, %u failed with Accurate cache timing.\n",passed,failed);
    return failed ? 1 : 0;
}
