"""Execute static ARM Linux check binaries with Unicorn (pip install unicorn).

This minimal single-threaded syscall harness exercises emitted A32/NEON code;
it is not a replacement for testing a complete core on an ARM Linux machine.
"""
import argparse
import os
from pathlib import Path
import struct
import sys
import time

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR
from unicorn.arm_const import *

PAGE = 4096
def align(value):
    return (value + PAGE - 1) & -PAGE

def run(path, limit):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
        raise ValueError('expected a little-endian ARM ELF32 executable')
    entry, phoff = struct.unpack_from('<II', data, 24)
    phsize, phcount = struct.unpack_from('<HH', data, 42)
    vm = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    vm.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A7)
    vm.reg_write(UC_ARM_REG_C1_C0_2, 0x00f00000)
    vm.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    mapped = set()
    def map_range(address, size, protection=7):
        for page in range(address & -PAGE, align(address + size), PAGE):
            if page not in mapped:
                vm.mem_map(page, PAGE, protection)
                mapped.add(page)
    def unmap_range(address, size):
        for page in range(address & -PAGE, align(address + size), PAGE):
            if page in mapped:
                vm.mem_unmap(page, PAGE)
                mapped.remove(page)
    high, phdr = 0, 0
    for index in range(phcount):
        kind, offset, address, _, filesz, memsz, flags, _ = struct.unpack_from('<8I', data, phoff + phsize*index)
        if kind == 3:
            raise ValueError('dynamic executables are unsupported; build checks with -static')
        if kind == 6:
            phdr = address
        if kind != 1:
            continue
        map_range(address, memsz)
        vm.mem_write(address, data[offset:offset+filesz])
        high = max(high, align(address + memsz))
        if offset <= phoff < offset+filesz:
            phdr = address+phoff-offset
    stack_base, stack_size = 0x7e000000, 8*1024*1024
    map_range(stack_base, stack_size)
    stack = stack_base+stack_size-4096
    program = stack+1024
    vm.mem_write(program, os.fsencode(path)+b'\0')
    random = stack+2048
    vm.mem_write(random, bytes(range(16)))
    auxiliary = [(3, phdr), (4, phsize), (5, phcount), (6, PAGE), (7, 0), (9, entry),
                 (11, 1000), (12, 1000), (13, 1000), (14, 1000), (16, 0x1fffff),
                 (23, 0), (25, random), (31, program), (0, 0)]
    words = [1, program, 0, 0]+[word for pair in auxiliary for word in pair]
    vm.mem_write(stack, struct.pack('<'+'I'*len(words), *words))
    vm.reg_write(UC_ARM_REG_SP, stack)
    heap, next_map = high, 0x30000000
    result, calls = None, []
    started = time.monotonic()
    def read32(address):
        return struct.unpack('<I', vm.mem_read(address, 4))[0]
    def syscall(machine, interrupt, _):
        nonlocal heap, next_map, result
        if interrupt != 2:
            raise RuntimeError(f'unexpected ARM interrupt {interrupt}')
        number = vm.reg_read(UC_ARM_REG_R7)
        a = [vm.reg_read(UC_ARM_REG_R0+i) for i in range(6)]
        calls.append((number, a[:3]))
        del calls[:-20]
        answer = 0
        if number in (1, 248):
            result = a[0]
            vm.emu_stop()
        elif number == 4:
            stream = sys.stdout.buffer if a[0] == 1 else sys.stderr.buffer
            stream.write(bytes(vm.mem_read(a[1], a[2])))
            stream.flush()
            answer = a[2]
        elif number == 146:
            answer = 0
            stream = sys.stdout.buffer if a[0] == 1 else sys.stderr.buffer
            for index in range(a[2]):
                base, size = struct.unpack('<II', vm.mem_read(a[1]+index*8, 8))
                stream.write(bytes(vm.mem_read(base, size)))
                answer += size
            stream.flush()
        elif number == 45:
            if a[0]:
                if a[0] > heap:
                    map_range(heap, a[0]-heap)
                heap = a[0]
            answer = heap
        elif number == 192:
            address = a[0] if a[0] else next_map
            if not a[0]:
                next_map = align(address+a[1])+PAGE
            map_range(address, a[1], a[2])
            answer = address
        elif number == 91:
            unmap_range(a[0], a[1])
        elif number == 125:
            vm.mem_protect(a[0], align(a[1]), a[2])
        elif number == 163:
            address = next_map
            next_map = align(address+a[2])+PAGE
            map_range(address, a[2])
            vm.mem_write(address, bytes(vm.mem_read(a[0], min(a[1], a[2]))))
            unmap_range(a[0], a[1])
            answer = address
        elif number == 0xf0005:
            vm.reg_write(UC_ARM_REG_C13_C0_3, a[0])
        elif number == 0xf0002:
            if a[1] > a[0]:
                vm.ctl_remove_cache(a[0], a[1]-1)
        elif number in (20, 224, 256):
            answer = 1
        elif number in (24, 47, 49, 50):
            answer = 1000
        elif number in (174, 175, 186, 220, 240, 338):
            pass
        elif number in (263, 403):
            now = time.monotonic()
            vm.mem_write(a[1], struct.pack('<qq' if number == 403 else '<ii', int(now), int(now%1*1e9)))
        elif number == 384:
            vm.mem_write(a[0], bytes((i*17+13)&255 for i in range(a[1])))
            answer = a[1]
        elif number == 54:
            answer = -25
        elif number == 3:
            answer = 0
        elif number in (5, 195, 197):
            answer = -2
        else:
            raise RuntimeError(f'unimplemented ARM Linux syscall {number}: {a}')
        vm.reg_write(UC_ARM_REG_R0, answer & 0xffffffff)
    vm.hook_add(UC_HOOK_INTR, syscall)
    try:
        vm.emu_start(entry, 0, count=limit)
        if result is None:
            raise RuntimeError('instruction budget exhausted')
    except (UcError, RuntimeError):
        pc = vm.reg_read(UC_ARM_REG_PC)
        print(f'ARM32 PC={pc:08x}; recent syscalls={calls}', file=sys.stderr)
        raise
    print(f'ARM32 emulation: exit={result}, {time.monotonic()-started:.2f}s', file=sys.stderr)
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--instructions', type=int, default=2_000_000_000)
    args = parser.parse_args()
    sys.exit(run(args.binary, args.instructions))
