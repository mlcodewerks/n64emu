"""Run arm_vector_fixtures.cpp output using Unicorn's ARMv7/NEON engine."""
import struct
import sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import *

def check(path):
    vm=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    vm.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A7)
    vm.reg_write(UC_ARM_REG_C1_C0_2,0x00f00000)
    vm.reg_write(UC_ARM_REG_FPEXC,0x40000000)
    code,state,memory,stack,end=0x10000000,0x20000000,0x21000000,0x70000000,0x100ffff0
    for base in (code,state,memory,stack):vm.mem_map(base,1024*1024)
    count=0
    with open(path,'rb') as source:
        def word():return struct.unpack('<I',source.read(4))[0]
        assert word()==0x33505352
        size,cp2,cp2_size,scalar,tables=word(),word(),word(),word(),word()
        mapped=set()
        for _ in range(tables):
            address,length=word(),word()
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
                vm.reg_write(UC_ARM_REG_SP,stack+0x80000)
                vm.reg_write(UC_ARM_REG_LR,end)
                vm.reg_write(UC_ARM_REG_R0,state)
                vm.reg_write(UC_ARM_REG_R1,memory)
                vm.reg_write(UC_ARM_REG_R2,tag&4095)
                vm.emu_start(code,end,count=100000)
                actual=bytes(vm.mem_read(state+cp2,cp2_size))
                if actual!=expected:
                    changes=[(i,actual[i:i+2].hex(),expected[i:i+2].hex()) for i in range(0,cp2_size,2) if actual[i:i+2]!=expected[i:i+2]]
                    raise AssertionError(f'A32 vector mismatch op={tag>>24:02x} e={(tag>>16)&15} alias={(tag>>8)&7} sample={sample}: {changes[:16]}')
                if expected_scalar is not None:
                    assert struct.unpack('<I',vm.mem_read(state+scalar,4))[0]==expected_scalar, f'A32 COP2 mismatch tag={tag:08x}'
                if expected_memory is not None:
                    assert bytes(vm.mem_read(memory,4096))==expected_memory, f'A32 memory mismatch tag={tag:08x}'
                count+=1
    print(f'ARM32 NEON: {count} native vector, COP2 and memory differential cases passed.')

if __name__=='__main__':check(sys.argv[1])
