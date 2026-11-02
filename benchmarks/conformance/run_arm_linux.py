"""Single-threaded ARM Linux test runner using Unicorn; no native ARM hardware claim.

Loads static ARMv7/AArch64 ELF files, including the real core and its generated
JIT code. Implements file IO and memory syscalls used by these tests. Unsupported
syscalls fail explicitly; guest threads are not emulated. File writes are limited
to the conformance output directory.
"""
import argparse
from collections import deque
import errno
import os
from pathlib import Path
import struct
import sys
import threading
import time
from unicorn import Uc, UC_ARCH_ARM, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_HOOK_CODE
from unicorn import arm_const as a32, arm64_const as a64

PAGE = 4096
ALIGN = lambda n: (n + PAGE - 1) & -PAGE


def run(binary, arguments, cwd, instruction_limit, seconds):
    data = binary.read_bytes()
    machine = struct.unpack_from('<H', data, 18)[0]
    wide = machine == 183
    if data[:4] != b'\x7fELF' or machine not in (40, 183) or data[5] != 1:
        raise ValueError('expected static little-endian ARM ELF')
    vm = Uc(UC_ARCH_ARM64 if wide else UC_ARCH_ARM, UC_MODE_ARM)
    vm.ctl_set_cpu_model(a64.UC_CPU_ARM64_A72 if wide else a32.UC_CPU_ARM_CORTEX_A7)
    if not wide:
        vm.reg_write(a32.UC_ARM_REG_C1_C0_2, 0x00f00000)
        vm.reg_write(a32.UC_ARM_REG_FPEXC, 0x40000000)
    regs = [getattr(a64, 'UC_ARM64_REG_X' + str(i)) for i in range(9)] if wide else [a32.UC_ARM_REG_R0 + i for i in range(8)]
    word, bits = ('Q', 64) if wide else ('I', 32)
    mask = (1 << bits) - 1
    entry, phoff = struct.unpack_from('<QQ' if wide else '<II', data, 24)
    phsize, phcount = struct.unpack_from('<HH', data, 54 if wide else 42)
    mapped = set()
    permissions = {}
    mapping_events = []
    invalidations = []
    trace = deque(maxlen=128)
    trace_page = int(os.environ.get('N64EMU_ARM_TRACE_PAGE', '0'), 0)
    if wide and trace_page:
        def instruction_trace(uc, address, size, _):
            trace.append((hex(address), bytes(uc.mem_read(address, size)).hex(),
                *[hex(uc.reg_read(reg)) for reg in (a64.UC_ARM64_REG_X0, a64.UC_ARM64_REG_X19,
                                                   a64.UC_ARM64_REG_X9, a64.UC_ARM64_REG_X11)]))
        vm.hook_add(UC_HOOK_CODE, instruction_trace, begin=trace_page, end=trace_page + PAGE - 1)

    def map_range(address, size, protection=7):
        start, end = address & -PAGE, ALIGN(address + size)
        # Map contiguous holes together; mapping a 512 MiB core RAM reservation
        # one page per Python call is unnecessarily expensive.
        cursor = start
        while cursor < end:
            if cursor in mapped:
                cursor += PAGE
                continue
            stop = cursor + PAGE
            while stop < end and stop not in mapped:
                stop += PAGE
            vm.mem_map(cursor, stop - cursor, protection)
            mapped.update(range(cursor, stop, PAGE))
            permissions.update((page, protection) for page in range(cursor, stop, PAGE))
            cursor = stop

    def unmap_range(address, size):
        nonlocal next_map
        start, end = address & -PAGE, ALIGN(address + size)
        invalidations.append((start, end - 1))
        cursor = start
        while cursor < end:
            if cursor not in mapped:
                cursor += PAGE
                continue
            stop = cursor + PAGE
            while stop < end and stop in mapped:
                stop += PAGE
            vm.mem_unmap(cursor, stop - cursor)
            mapped.difference_update(range(cursor, stop, PAGE))
            for page in range(cursor, stop, PAGE):
                permissions.pop(page, None)
            cursor = stop
        if address >= 0x30000000:
            next_map = min(next_map, address)

    high, phdr = 0, 0
    for i in range(phcount):
        offset = phoff + phsize * i
        if wide:
            kind, flags, off, addr, _, filesz, memsz, _ = struct.unpack_from('<II6Q', data, offset)
        else:
            kind, off, addr, _, filesz, memsz, flags, _ = struct.unpack_from('<8I', data, offset)
        if kind == 3:
            raise ValueError('dynamic linker unsupported; build with -static')
        if kind == 6:
            phdr = addr
        if kind == 1:
            map_range(addr, memsz)
            vm.mem_write(addr, data[off:off + filesz])
            high = max(high, ALIGN(addr + memsz))
            if off <= phoff < off + filesz:
                phdr = addr + phoff - off
    stack = 0x7e000000
    map_range(stack, 8 << 20)
    sp, strings = stack + (8 << 20) - 65536, stack + (8 << 20) - 32768
    argv = []
    for arg in [str(binary), *arguments]:
        blob = arg.encode() + b'\0'
        vm.mem_write(strings, blob)
        argv.append(strings)
        strings += len(blob)
    random = strings + 16
    vm.mem_write(random, bytes(range(16)))
    aux = [(3, phdr), (4, phsize), (5, phcount), (6, PAGE), (7, 0), (9, entry),
           (11, 1000), (12, 1000), (13, 1000), (14, 1000), (16, 3 if wide else 0x1fffff),
           (23, 0), (25, random), (31, argv[0]), (0, 0)]
    words = [len(argv), *argv, 0, 0, *[n for pair in aux for n in pair]]
    vm.mem_write(sp, struct.pack('<' + word * len(words), *words))
    vm.reg_write(a64.UC_ARM64_REG_SP if wide else a32.UC_ARM_REG_SP, sp)
    heap, next_map = high, 0x30000000
    result, calls, files = None, [], {}
    started = time.monotonic()
    output_root = Path(__file__).resolve().parent / 'out'
    ids = ({'exit': 93, 'exit_group': 94, 'read': 63, 'readv': 65, 'write': 64, 'writev': 66,
            'brk': 214, 'mmap': 222, 'munmap': 215, 'mprotect': 226, 'mremap': 216,
            'openat': 56, 'close': 57, 'lseek': 62, 'fstat': 80, 'statat': 79,
            'clock': 113, 'timeval': 169, 'random': 278, 'ioctl': 29, 'getaffinity': 123}
           if wide else {'exit': 1, 'exit_group': 248, 'read': 3, 'readv': 145, 'write': 4, 'writev': 146,
            'brk': 45, 'mmap': 192, 'munmap': 91, 'mprotect': 125, 'mremap': 163,
            'open': 5, 'openat': 322, 'close': 6, 'lseek': 19, 'llseek': 140,
            'fstat': 197, 'statat': 327, 'clock': 263, 'clock64': 403, 'timeval': 78,
            'random': 384, 'ioctl': 54, 'getaffinity': 242})
    names = {number: name for name, number in ids.items()}

    def cstring(address):
        text = bytearray()
        for i in range(32768):
            byte = vm.mem_read(address + i, 1)[0]
            if not byte:
                return text.decode()
            text.append(byte)
        raise ValueError('unterminated guest path')

    def path_at(address):
        path = Path(cstring(address))
        return (path if path.is_absolute() else cwd / path).resolve()

    def output(fd, blob):
        if fd in (1, 2):
            stream = sys.stdout.buffer if fd == 1 else sys.stderr.buffer
            stream.write(blob)
            stream.flush()
        else:
            files[fd].write(blob)

    def write_stat(address, size):
        blob = bytearray(128)
        struct.pack_into('<I', blob, 16, 0o100644)
        struct.pack_into('<q', blob, 48, size)
        struct.pack_into('<I', blob, 56, PAGE)
        struct.pack_into('<q', blob, 64, (size + 511) // 512)
        vm.mem_write(address, bytes(blob))

    restart_translation = False
    pending_memory_syscall = False
    def syscall(machine_vm, interrupt, userdata):
        nonlocal heap, next_map, result, restart_translation, pending_memory_syscall
        if interrupt != 2:
            raise RuntimeError(f'unexpected ARM interrupt {interrupt}')
        number = vm.reg_read(regs[8 if wide else 7])
        a = [vm.reg_read(regs[i]) for i in range(6)]
        name = names.get(number)
        if userdata is not True and (name in ('brk', 'mmap', 'munmap', 'mprotect', 'mremap') or
                                    (not wide and number == 0xf0002)):
            # Defer mapping changes until execution is stopped. Updating an
            # executable mapping inside the interrupt callback can invalidate
            # Unicorn's active translation before its architectural PC syncs.
            pending_memory_syscall = True
            restart_translation = True
            vm.emu_stop()
            return
        calls.append((number, a[:3]))
        del calls[:-20]
        answer = 0
        cache_changed = False
        try:
            if name in ('exit', 'exit_group'):
                result = a[0]
                vm.emu_stop()
            elif name == 'read':
                blob = files[a[0]].read(a[2]) if a[0] in files else b''
                vm.mem_write(a[1], blob)
                answer = len(blob)
            elif name == 'write':
                output(a[0], bytes(vm.mem_read(a[1], a[2])))
                answer = a[2]
            elif name == 'readv':
                for i in range(a[2]):
                    address, size = struct.unpack('<' + word * 2, vm.mem_read(a[1] + i * (16 if wide else 8), 16 if wide else 8))
                    blob = files[a[0]].read(size)
                    vm.mem_write(address, blob)
                    answer += len(blob)
                    if len(blob) < size:
                        break
            elif name == 'writev':
                for i in range(a[2]):
                    address, size = struct.unpack('<' + word * 2, vm.mem_read(a[1] + i * (16 if wide else 8), 16 if wide else 8))
                    output(a[0], bytes(vm.mem_read(address, size)))
                    answer += size
            elif name in ('open', 'openat'):
                index = 0 if name == 'open' else 1
                path, flags = path_at(a[index]), a[index + 1]
                writing = flags & 3
                if writing and not path.is_relative_to(output_root):
                    raise PermissionError(errno.EACCES, 'guest writes restricted to conformance/out', path)
                fd = max([2, *files]) + 1
                files[fd] = path.open('w+b' if flags & 512 or (flags & 64 and not path.exists()) else 'r+b' if writing else 'rb')
                answer = fd
            elif name == 'close':
                if a[0] in files:
                    files.pop(a[0]).close()
            elif name == 'lseek':
                offset = a[1] - (1 << bits) if a[1] & (1 << (bits - 1)) else a[1]
                answer = files[a[0]].seek(offset, a[2])
            elif name == 'llseek':
                offset = (a[1] << 32) | a[2]
                if offset & (1 << 63):
                    offset -= 1 << 64
                position = files[a[0]].seek(offset, a[4])
                vm.mem_write(a[3], struct.pack('<Q', position))
            elif name == 'fstat':
                f = files.get(a[0])
                position = f.tell() if f else 0
                write_stat(a[1], f.seek(0, 2) if f else 0)
                if f:
                    f.seek(position)
            elif name == 'statat':
                write_stat(a[2], path_at(a[1]).stat().st_size)
            elif name == 'brk':
                if a[0]:
                    if a[0] > heap:
                        map_range(heap, a[0] - heap)
                    heap = a[0]
                answer = heap
            elif name == 'mmap':
                cache_changed = True
                address = a[0] or next_map
                if a[3] & 16:
                    unmap_range(address, a[1])
                else:
                    # A non-FIXED address is a hint, never permission to
                    # overlap a live reservation (notably the JIT arena).
                    while any(page in mapped for page in range(address & -PAGE, ALIGN(address + a[1]), PAGE)):
                        address = max(next_map, ALIGN(address + a[1]) + PAGE)
                    next_map = max(next_map, ALIGN(address + a[1]) + PAGE)
                map_range(address, a[1], a[2])
                invalidations.append((address & -PAGE, ALIGN(address + a[1]) - 1))
                mapping_events.append(('mmap', address, a[1], a[2]))
                if a[4] in files:
                    file = files[a[4]]
                    position = file.tell()
                    file.seek(a[5] * (1 if wide else PAGE))
                    vm.mem_write(address, file.read(a[1]))
                    file.seek(position)
                answer = address
            elif name == 'munmap':
                unmap_range(a[0], a[1])
                cache_changed = True
            elif name == 'mprotect':
                vm.mem_protect(a[0], ALIGN(a[1]), a[2])
                mapping_events.append(('mprotect', a[0], ALIGN(a[1]), a[2]))
                permissions.update((page, a[2]) for page in range(a[0], a[0] + ALIGN(a[1]), PAGE))
                invalidations.append((a[0], a[0] + ALIGN(a[1]) - 1))
                cache_changed = True
            elif name == 'mremap':
                address = next_map
                while any(page in mapped for page in range(address, ALIGN(address + a[2]), PAGE)):
                    address = ALIGN(address + a[2]) + PAGE
                next_map = ALIGN(address + a[2]) + PAGE
                map_range(address, a[2], permissions.get(a[0] & -PAGE, 3))
                vm.mem_write(address, bytes(vm.mem_read(a[0], min(a[1], a[2]))))
                unmap_range(a[0], a[1])
                answer = address
                cache_changed = True
            elif name in ('clock', 'clock64', 'timeval'):
                now = time.time() if name == 'timeval' else time.monotonic()
                address = a[0] if name == 'timeval' else a[1]
                scale = 1e6 if name == 'timeval' else 1e9
                fmt = '<qq' if wide or name == 'clock64' else '<ii'
                if address:
                    vm.mem_write(address, struct.pack(fmt, int(now), int(now % 1 * scale)))
            elif name == 'random':
                vm.mem_write(a[0], bytes((i * 17 + 13) & 255 for i in range(a[1])))
                answer = a[1]
            elif name == 'ioctl':
                answer = -errno.ENOTTY
            elif name == 'getaffinity':
                vm.mem_write(a[2], b'\1' + bytes(a[1] - 1))
                answer = min(a[1], 8)
            elif not wide and number == 0xf0005:
                vm.reg_write(a32.UC_ARM_REG_C13_C0_3, a[0])
            elif not wide and number == 0xf0002:
                if a[1] > a[0]:
                    invalidations.append((a[0], a[1] - 1))
                    cache_changed = True
            elif number in ((172, 178, 96) if wide else (20, 224, 256)):
                answer = 1
            elif number in ((174, 175, 176, 177) if wide else (24, 47, 49, 50)):
                answer = 1000
            elif number in ((99, 134, 135, 131, 98, 261, 122, 25) if wide else (174, 175, 186, 220, 240, 338, 241, 55, 221)):
                # Single-thread process setup, signal masks, limits, affinity
                # and fcntl. No test or core workers are launched in this mode.
                pass
            else:
                raise RuntimeError(f'unimplemented syscall {number}: {a}')
        except OSError as error:
            answer = -(error.errno or errno.EIO)
        vm.reg_write(regs[0], answer & mask)
        if cache_changed:
            restart_translation = True

    def invalid_memory(uc, access, address, size, value, _):
        pc = uc.reg_read(a64.UC_ARM64_REG_PC if wide else a32.UC_ARM_REG_PC)
        debug_regs = [getattr(a64, 'UC_ARM64_REG_X' + str(i)) for i in range(31)] if wide else [a32.UC_ARM_REG_R0 + i for i in range(16)]
        values = [hex(uc.reg_read(reg)) for reg in debug_regs]
        print(f'unmapped access={access} address={address:x} size={size} PC={pc:x} registers={values}', file=sys.stderr)
        print('mapping history:', [event for event in mapping_events if event[1] <= address < event[1] + event[2]][-12:], file=sys.stderr)
        print('PC mapping history:', [event for event in mapping_events if event[1] <= pc < event[1] + event[2]][-12:], file=sys.stderr)
        print('PC bytes:', bytes(uc.mem_read(pc - 32, 68)).hex(), file=sys.stderr)
        if wide:
            start = pc & -PAGE
            uc_bytes = bytes(uc.mem_read(start, PAGE))
            (output_root / 'arm64-fault-code.bin').write_bytes(uc_bytes)
            print('code dump base:', hex(start), file=sys.stderr)
        print('LR:', hex(uc.reg_read(a64.UC_ARM64_REG_LR if wide else a32.UC_ARM_REG_LR)), file=sys.stderr)
        if trace:
            print('instruction trace (PC, encoding, x0, x19, x9, x11):', list(trace), file=sys.stderr)
        return False
    vm.hook_add(UC_HOOK_MEM_INVALID, invalid_memory)
    vm.hook_add(UC_HOOK_INTR, syscall)
    # One watchdog for the run. Creating Unicorn's native timeout worker for
    # every publication slice makes thousands of short slices very expensive.
    watchdog = threading.Timer(max(0, seconds - (time.monotonic() - started)), vm.emu_stop)
    watchdog.daemon = True
    watchdog.start()
    try:
        resume = entry
        while result is None:
            remaining = seconds - (time.monotonic() - started)
            if remaining <= 0:
                break
            restart_translation = False
            vm.emu_start(resume, 0, count=instruction_limit)
            if not restart_translation:
                break
            if pending_memory_syscall:
                pending_memory_syscall = False
                syscall(vm, 2, True)
            # Finish invalidation outside the active translation as well.
            for start, end in invalidations:
                vm.ctl_remove_cache(start, end)
            invalidations.clear()
            resume = vm.reg_read(a64.UC_ARM64_REG_PC if wide else a32.UC_ARM_REG_PC)
        if result is None:
            pc = vm.reg_read(a64.UC_ARM64_REG_PC if wide else a32.UC_ARM_REG_PC)
            raise RuntimeError('guest branched to null; not a test pass' if pc == 0 else 'instruction or time budget exhausted; not a test pass')
    finally:
        watchdog.cancel()
        for file in files.values():
            file.close()
        pc = vm.reg_read(a64.UC_ARM64_REG_PC if wide else a32.UC_ARM_REG_PC)
        print(f'ARM{64 if wide else 32} exit={result} PC={pc:x} elapsed={time.monotonic()-started:.2f}s; recent syscalls={calls}', file=sys.stderr)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--cwd', type=Path, default=Path.cwd())
    parser.add_argument('--instructions', type=int, default=10_000_000_000,
                        help='Maximum instructions per execution slice between mapping/cache syscalls')
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    sys.exit(run(args.binary.resolve(), args.arguments, args.cwd.resolve(), args.instructions, args.seconds))
