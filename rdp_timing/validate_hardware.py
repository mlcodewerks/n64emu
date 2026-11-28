#!/usr/bin/env python3
"""Replay the NTSC-U DK Rap and compare it with the UltraHDMI capture rrA1jccFYbA."""

from array import array
from argparse import ArgumentParser
from concurrent.futures import ThreadPoolExecutor
from csv import DictReader
from ctypes import CDLL, POINTER, byref, c_char_p, c_double, c_float, c_size_t, cast, sizeof
from hashlib import md5
from math import exp, log1p, pi
from pathlib import Path
from statistics import median
from subprocess import check_output, run
import sys

ROOT = Path(__file__).resolve().parents[1]
ROM_MD5 = '9ec41abf2519fc386cadd0731f6e868c'
FIELDS = 11400
COUNT_HZ = 46875000
SAMPLE_RATE = 44100
FEATURES = 24 * 18
SCAN_BYTES = 96 * 72
AUDIO_ANCHORS = (18, 26, 30, 42, 50, 62, 78, 90, 110, 122, 130, 158, 182)
VIDEO_ANCHORS = (18, 36, 42, 46, 50, 58, 62, 70, 78, 90, 98, 114, 130, 142, 146, 154, 158, 162, 174, 182)


def build_tools(cc, output):
    library, runner = output / 'av_match.dll', output / 'libretro_runner.exe'
    run([cc, '-O2', '-shared', ROOT / 'rdp_timing/tests/av_match.c', '-o', library, '-lm'], check=True)
    run([cc, '-O2', '-I', ROOT / 'deps/libretro-common/include',
         ROOT / 'benchmarks/conformance/libretro_runner.c', '-o', runner], check=True)
    match = CDLL(str(library))
    floats, doubles = POINTER(c_float), POINTER(c_double)
    match.video_features.argtypes = [c_char_p, floats, c_size_t]
    match.video_scores.argtypes = [floats, floats, c_size_t, floats]
    match.audio_scores.argtypes = [doubles, doubles, c_size_t, c_size_t, doubles]
    return match, runner


def decode(ffmpeg, path, *options, raw_pcm=False):
    input_options = ['-f', 's16le', '-ar', str(SAMPLE_RATE), '-ac', '2'] if raw_pcm else []
    return check_output([ffmpeg, '-v', 'error', *input_options, '-i', path, *options, '-'])


def audio_features(raw):
    samples = array('h')
    samples.frombytes(raw)
    if sys.byteorder != 'little':
        samples.byteswap()
    a, b, c = (1 - exp(-2 * pi * f / 8000) for f in (140, 500, 1700))
    low = mid = high = 0.0
    features = []
    for offset in range(0, len(samples) - 79, 80):
        energy = [0.0] * 4
        for sample in samples[offset:offset + 80]:
            low += a * (sample - low)
            mid += b * (sample - mid)
            high += c * (sample - high)
            energy[0] += low * low
            energy[1] += (mid - low) ** 2
            energy[2] += (high - mid) ** 2
            energy[3] += (sample - high) ** 2
        features.extend(log1p(value / 800) for value in energy)
    return (c_double * len(features))(*features)


def video_features(match, raw):
    if len(raw) % SCAN_BYTES:
        raise ValueError('incomplete video frame')
    count = len(raw) // SCAN_BYTES
    result = (c_float * (count * FEATURES))()
    match.video_features(raw, result, count)
    return result


def at(values, index, kind):
    return cast(byref(values, index * sizeof(kind)), POINTER(kind))


def audio_match(match, reference, replay, second, expected, search):
    width, half, bands = 151, 75, 4
    start = round(second * 100) - half
    low = max(0, round((expected - search) * 100) - half)
    high = min(len(replay) // bands - width, round((expected + search) * 100) - half)
    if start < 0 or (start + width) * bands > len(reference) or low > high:
        return None
    scores = (c_double * (high - low + 1))()
    match.audio_scores(at(reference, start * bands, c_double), at(replay, low * bands, c_double),
                       width, len(scores), scores)
    best = max(range(len(scores)), key=scores.__getitem__)
    other = max((value for index, value in enumerate(scores) if abs(index - best) > 100), default=-1.0)
    return (best + low + half) / 100, scores[best], scores[best] - other


def video_match(match, reference, replay, second, expected):
    center = round(second * 30)
    low = max(10, round((expected - 3) * 60))
    high = min(len(replay) // FEATURES - 11, round((expected + 3) * 60))
    if (center + 6) * FEATURES > len(reference) or high <= low:
        return None
    scores = (c_float * (high - low))()
    match.video_scores(at(reference, (center - 5) * FEATURES, c_float),
                       at(replay, (low - 10) * FEATURES, c_float), len(scores), scores)
    best = max(range(len(scores)), key=lambda i: (scores[i], -abs(i + low - expected * 60)))
    other = max((value for index, value in enumerate(scores) if abs(index - best) > 60), default=-1.0)
    return best + low, scores[best], scores[best] - other


def check(mode, output, ffmpeg, match, reference_video, reference_audio, max_lag):
    with (output / 'timing.csv').open(newline='') as stream:
        fields = [{key: int(value) for key, value in row.items()} for row in DictReader(stream)]
    scan = (output / 'scan.gray').read_bytes()
    pcm = output / 'audio.pcm'
    if len(fields) != FIELDS or len(scan) != FIELDS * SCAN_BYTES:
        raise ValueError(f'{mode}: incomplete replay')
    if pcm.stat().st_size != fields[-1]['audio_frames'] * 4:
        raise ValueError(f'{mode}: audio file and reported sample count disagree')
    replay_video = video_features(match, scan)
    replay_audio = audio_features(decode(ffmpeg, pcm, '-vn', '-ac', '1', '-ar', '8000', '-f', 's16le', raw_pcm=True))

    offsets = []
    for second in AUDIO_ANCHORS:
        found = audio_match(match, reference_audio, replay_audio, second, second, 8)
        if found and found[1] >= 0.92 and found[2] >= 0.12:
            offsets.append(found[0] - second)
    if len(offsets) < 6 or max(offsets) - min(offsets) > 0.04:
        raise ValueError(f'{mode}: audio match was sparse or drifted: {offsets}')
    offset = median(offsets)

    gaps = {}
    for second in VIDEO_ANCHORS:
        found = video_match(match, reference_video, replay_video, second, second + offset)
        if found and found[1] >= 0.80 and found[2] >= 0.03:
            index = found[0]
            # The indexed frame was returned after the preceding field's samples.
            pts = fields[index - 1]['audio_frames'] / SAMPLE_RATE
            gaps[second] = pts - second - offset
    if len(gaps) < 12 or not {50, 90, 182} <= gaps.keys():
        raise ValueError(f'{mode}: insufficient shared video landmarks: {list(gaps)}')

    song = [row for row in fields if 18 + offset <= row['audio_frames']/SAMPLE_RATE <= 182 + offset]
    if not song or any(row['ai_divider'] != 2207 or not row['ai_status'] & 0x40000000 for row in song):
        raise ValueError(f'{mode}: AI DMA went idle or changed divider during the song')
    if any(row['ai_completion'] < row['vi_boundary'] - 128 or row['ai_remaining'] > 0x3fff8 or
           bool(row['ai_status'] & 0x80000000) != bool(row['ai_queued']) for row in song):
        raise ValueError(f'{mode}: AI FIFO length, occupancy or completion is inconsistent')
    sample_error = (song[-1]['audio_frames'] - song[0]['audio_frames'] -
                    (song[-1]['clock'] - song[0]['clock']) * SAMPLE_RATE / COUNT_HZ)
    if abs(sample_error) > 4:
        raise ValueError(f'{mode}: host audio sample budget drifted by {sample_error:.2f} samples')
    dp_fields = [row for row in song if row['dp_irq']]
    if not dp_fields or any(row['dp_irq'] < row['vi_boundary'] - 128 for row in dp_fields):
        raise ValueError(f'{mode}: missing or overdue DP interrupts')
    swaps = sum(a['vi_origin'] != b['vi_origin'] for a, b in zip(song, song[1:]))
    peak = max(abs(gap) for gap in gaps.values())
    for second, gap in gaps.items():
        print(f'{mode:3}  {second:5.1f} s  video minus audio {gap * 1000:+.1f} ms')
    print(f'{mode}: audio offset spread {(max(offsets) - min(offsets)) * 1000:.1f} ms, '
          f'{swaps} framebuffer changes, {len(dp_fields)} pending DP fields, '
          f'{sum(bool(row["rsp_dma"]) for row in song)} pending RSP DMA fields, '
          f'{sample_error:+.2f} samples of AI budget error')
    if peak > max_lag:
        raise ValueError(f'{mode}: peak A/V difference {peak:.3f} s exceeded {max_lag:.3f} s')


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True, help='Windows HEADLESS_BENCHMARK=1 DLL')
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True, help='local copy of UltraHDMI capture rrA1jccFYbA')
    parser.add_argument('--out', type=Path, default=ROOT / 'tests/.build/hardware-dk64')
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--crop', default='448:330:94:14', help='crop for the 640×360 capture')
    parser.add_argument('--max-lag-on', type=float, default=0.25)
    parser.add_argument('--max-lag-off', type=float, default=0.20)
    args = parser.parse_args()
    args.core, args.rom, args.reference, output = (p.resolve() for p in (args.core,args.rom,args.reference,args.out))
    if md5(args.rom.read_bytes()).hexdigest() != ROM_MD5:
        parser.error('the reference replay needs the original NTSC-U .z64 dump')
    output.mkdir(parents=True, exist_ok=True)
    match, runner = build_tools(args.cc, output)
    crop = 'crop=' + args.crop + ',' if args.crop else ''
    reference_video = video_features(match, decode(args.ffmpeg, args.reference, '-t', '185', '-an', '-vf',
                                                   crop + 'fps=30,scale=96:72:flags=area,format=gray', '-f', 'rawvideo'))
    reference_audio = audio_features(decode(args.ffmpeg, args.reference, '-t', '185', '-vn', '-ac', '1',
                                            '-ar', '8000', '-f', 's16le'))

    def replay(mode):
        folder = output / mode
        folder.mkdir(exist_ok=True)
        flags = ['--timing-off'] if mode == 'off' else []
        with (folder / 'run.log').open('w') as log:
            run([runner, *flags, '--capture-av', args.core, args.rom, str(FIELDS)], cwd=folder,
                stdout=log, stderr=log, timeout=900, check=True)

    with ThreadPoolExecutor(2) as threads:
        list(threads.map(replay, ('on', 'off')))
    for mode, limit in [('on', args.max_lag_on), ('off', args.max_lag_off)]:
        check(mode, output / mode, args.ffmpeg, match, reference_video, reference_audio, limit)


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        sys.exit(str(error))
