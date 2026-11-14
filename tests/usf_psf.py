"""Exercise the integrated PSF loader with independent zlib-generated fixtures."""
import argparse
import ctypes as C
import io
from pathlib import Path
import struct
import zlib

OPEN = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_char_p)
READ = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t, C.c_size_t, C.c_void_p)
SEEK = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int64, C.c_int)
CLOSE = C.CFUNCTYPE(C.c_int, C.c_void_p)
TELL = C.CFUNCTYPE(C.c_long, C.c_void_p)
LOAD = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t)
INFO = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.c_char_p)
STATUS = C.CFUNCTYPE(None, C.c_void_p, C.c_char_p)


class Files(C.Structure):
    _fields_ = [('separators', C.c_char_p), ('context', C.c_void_p),
                ('open', OPEN), ('read', READ), ('seek', SEEK),
                ('close', CLOSE), ('tell', TELL)]


def psf(executable=b'', reserved=b'', tags=b'', compressed=None):
    data = zlib.compress(executable) if compressed is None else compressed
    return (b'PSF\x21' + struct.pack('<III', len(reserved), len(data), zlib.crc32(data))
            + reserved + data + (b'[TAG]' + tags if tags else b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=Path)
    args = parser.parse_args()
    library = C.CDLL(str(args.library.resolve()))
    load = library.usf_load_psf
    load.argtypes = [C.c_char_p, C.POINTER(Files), C.c_uint8, LOAD, C.c_void_p,
                     INFO, C.c_void_p, C.c_int, STATUS, C.c_void_p]
    load.restype = C.c_int
    fixtures, handles, blocks, tags, messages = {}, {}, [], [], []
    next_handle = 0

    @OPEN
    def open_file(context, name):
        nonlocal next_handle
        if name not in fixtures:
            return None
        next_handle += 1
        handles[next_handle] = io.BytesIO(fixtures[name])
        return next_handle

    @READ
    def read_file(buffer, size, count, handle):
        if not size:
            return 0
        data = handles[handle].read(size * count)
        C.memmove(buffer, data, len(data))
        return len(data) // size

    @SEEK
    def seek_file(handle, offset, whence):
        try:
            handles[handle].seek(offset, whence)
            return 0
        except (ValueError, OSError):
            return -1

    @CLOSE
    def close_file(handle):
        del handles[handle]
        return 0

    @TELL
    def tell_file(handle):
        return handles[handle].tell()

    @LOAD
    def section(context, executable, executable_size, reserved, reserved_size):
        blocks.append((C.string_at(executable, executable_size), C.string_at(reserved, reserved_size)))
        return 0

    @INFO
    def info(context, name, value):
        tags.append((name, value))
        return 0

    @STATUS
    def status(context, message):
        messages.append(message)

    callbacks = Files(b'/\\', None, open_file, read_file, seek_file, close_file, tell_file)

    def run(name=b'music/track.miniusf', metadata_only=False):
        blocks.clear()
        tags.clear()
        messages.clear()
        result = load(name, C.byref(callbacks), 0x21, LOAD() if metadata_only else section,
                      None, info, None, 1, status, None)
        assert not handles, 'PSF file handle leaked'
        return result

    # High expansion ratio forces the decompression buffer to grow and retry.
    large = b'ROM\0' * 300000
    fixtures.update({
        b'music/base.usflib': psf(large, b'base-reserved', b'artist=Library\n'),
        b'music/extra.usflib': psf(b'extra', b'extra-reserved'),
        b'music/track.miniusf': psf(b'track', b'track-reserved',
            b'_lib=base.usflib\n_lib2=extra.usflib\ntitle=Fixture\n'),
    })
    assert run() == 0x21
    assert blocks == [(large, b'base-reserved'), (b'track', b'track-reserved'),
                      (b'extra', b'extra-reserved')], 'Library order or decoded data differs'
    assert (b'title', b'Fixture') in tags and (b'artist', b'Library') in tags
    assert run(metadata_only=True) == 0x21 and not blocks
    fixtures[b'empty.usf'] = psf(compressed=b'')
    assert run(b'empty.usf') == 0x21 and blocks == [(b'', b'')]
    corrupt = bytearray(psf(b'payload'))
    corrupt[12] ^= 1
    fixtures[b'bad-crc.usf'] = bytes(corrupt)
    assert run(b'bad-crc.usf') < 0 and not blocks
    fixtures[b'truncated.usf'] = psf(b'payload')[:18]
    assert run(b'truncated.usf') < 0 and not blocks
    fixtures[b'bad-stream.usf'] = psf(compressed=b'not a deflate stream')
    assert run(b'bad-stream.usf') < 0 and not blocks
    fixtures[b'wrong-version.usf'] = b'PSF\x01' + psf()[4:]
    assert run(b'wrong-version.usf') < 0
    fixtures[b'missing-lib.usf'] = psf(tags=b'_lib=absent.usflib\n')
    assert run(b'missing-lib.usf') < 0
    fixtures[b'cycle.usf'] = psf(tags=b'_lib=cycle.usf\n')
    assert run(b'cycle.usf') < 0
    print('PASS PSF/miniz: library order, metadata, buffer growth, empty sections, CRC, '
          'truncation, invalid streams, missing libraries and recursion limits')


if __name__ == '__main__':
    main()
