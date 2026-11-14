"""Execute ARMv7/AArch64 vector fixtures with Unicorn; no hardware speed claim."""
import struct
import sys
from unicorn import Uc, UC_ARCH_ARM, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm_const import *
from unicorn import arm64_const as a64

def check(path):
    with open(path,'rb') as source:
        magic=struct.unpack('<I',source.read(4))[0]
    assert magic in (0x33505352,0x36505352)
    wide=magic==0x36505352
    vm=Uc(UC_ARCH_ARM64 if wide else UC_ARCH_ARM,UC_MODE_ARM)
    vm.ctl_set_cpu_model(a64.UC_CPU_ARM64_A72 if wide else UC_CPU_ARM_CORTEX_A7)
    if not wide:
        vm.reg_write(UC_ARM_REG_C1_C0_2,0x00f00000)
        vm.reg_write(UC_ARM_REG_FPEXC,0x40000000)
    sp,lr,r0,r1,r2=(a64.UC_ARM64_REG_SP,a64.UC_ARM64_REG_X30,a64.UC_ARM64_REG_X0,a64.UC_ARM64_REG_X1,a64.UC_ARM64_REG_X2) if wide else (UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)
    code,state,memory,stack,end=0x10000000,0x20000000,0x21000000,0x70000000,0x100ffff0
    for base in (code,state,memory,stack):vm.mem_map(base,1024*1024)
    count=0
    with open(path,'rb') as source:
        def word():return struct.unpack('<I',source.read(4))[0]
        assert word()==magic
        size,cp2,cp2_size,scalar,tables=word(),word(),word(),word(),word()
        mapped=set()
        for _ in range(tables):
            address=struct.unpack('<Q',source.read(8))[0] if wide else word()
            length=word()
            for page in range(address&-4096,(address+length+4095)&-4096,4096):
                if page not in mapped:vm.mem_map(page,4096);mapped.add(page)
            vm.mem_write(address,source.read(length))
        while True:
            tag=source.read(4)
            if not tag:break
            tag=struct.unpack('<I',tag)[0];length=word()
            vm.mem_write(code,source.read(length));vm.ctl_remove_cache(code,code+length)
            samples=word()
            for sample in range(samples):
                vm.mem_write(state,source.read(size));expected=source.read(cp2_size)
                expected_scalar=word() if tag&0xc0000000==0x40000000 else None
                expected_memory=None
                if tag&0x80000000:
                    vm.mem_write(memory,source.read(4096));expected_memory=source.read(4096)
                vm.reg_write(sp,stack+0x80000)
                vm.reg_write(lr,end)
                vm.reg_write(r0,state)
                vm.reg_write(r1,memory)
                vm.reg_write(r2,tag&4095)
                vm.emu_start(code,end,count=100000)
                pc=a64.UC_ARM64_REG_PC if wide else UC_ARM_REG_PC
                assert vm.reg_read(pc)==end, f'ARM fixture did not return: tag={tag:08x}'
                actual=bytes(vm.mem_read(state+cp2,cp2_size))
                if actual!=expected:
                    changes=[(i,actual[i:i+2].hex(),expected[i:i+2].hex()) for i in range(0,cp2_size,2) if actual[i:i+2]!=expected[i:i+2]]
                    raise AssertionError(f'A32 vector mismatch op={tag>>24:02x} e={(tag>>16)&15} alias={(tag>>8)&7} sample={sample}: {changes[:16]}')
                if expected_scalar is not None:
                    assert struct.unpack('<I',vm.mem_read(state+scalar,4))[0]==expected_scalar, f'A32 COP2 mismatch tag={tag:08x}'
                if expected_memory is not None:
                    assert bytes(vm.mem_read(memory,4096))==expected_memory, f'A32 memory mismatch tag={tag:08x}'
                count+=1
    print(f'ARM{64 if wide else 32} NEON: {count} native vector, COP2 and memory differential cases passed.')

if __name__=='__main__':check(sys.argv[1])
