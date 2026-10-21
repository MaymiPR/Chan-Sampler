"""Robustness against broken files: builds a corpus of corrupted samples and
presets from valid ones (truncations, flipped header bytes, absurd chunk sizes,
channel counts and rates, hostile marker / loop chunks, empty files and
garbage) and loads each through ChanSamplerRender in its own process, so a
crash or a hang shows per file. Loading may fail; it must not crash or hang.

    python corrupt_files.py <valid files...>
"""
import os, sys, struct, random, subprocess, tempfile
from lib import paths

HERE = paths.ROOT
RENDER = paths.RENDER_TOOL


def chunks(b):
    """(id, offset of the size field, size) of each RIFF chunk."""
    out, p = [], 12
    while p + 8 <= len(b):
        cid = b[p:p + 4]; size = struct.unpack('<I', b[p + 4:p + 8])[0]
        out.append((cid, p + 4, size))
        p += 8 + size + (size & 1)
    return out


def variants(name, b, rng):
    ext = os.path.splitext(name)[1]
    yield 'empty' + ext, b''
    yield 'garbage' + ext, bytes(rng.randrange(256) for _ in range(4096))
    for frac in (0.01, 0.05, 0.2, 0.5, 0.9):
        yield 'trunc%g' % frac + ext, b[:max(1, int(len(b) * frac))]
    for k in range(8):   # flip bytes in the header area
        c = bytearray(b)
        for _ in range(6):
            i = rng.randrange(min(len(c), 256)); c[i] = rng.randrange(256)
        yield 'flip%d' % k + ext, bytes(c)
    if b[:4] == b'RIFF':
        for cid, at, size in chunks(b):
            for label, val in (('huge', 0xFFFFFFF0), ('zero', 0), ('odd', size + 1)):
                c = bytearray(b); c[at:at + 4] = struct.pack('<I', val)
                yield '%s_%s%s' % (cid.decode('latin1').strip(), label, ext), bytes(c)
            if cid == b'fmt ':
                base = at + 4
                for label, off, fmt, val in (('ch0', 2, '<H', 0), ('ch64', 2, '<H', 64), ('rate0', 4, '<I', 0),
                                             ('rate1', 4, '<I', 1), ('rate4G', 4, '<I', 0xFFFFFFFF), ('bits0', 14, '<H', 0),
                                             ('bits7', 14, '<H', 7), ('tag99', 0, '<H', 99)):
                    c = bytearray(b); c[base + off:base + off + struct.calcsize(fmt)] = struct.pack(fmt, val)
                    yield 'fmt_%s%s' % (label, ext), bytes(c)
        # hostile marker / loop chunks appended to the file
        def add(cid, payload):
            body = b[12:] + cid + struct.pack('<I', len(payload)) + payload
            return b'RIFF' + struct.pack('<I', len(body) + 4) + b'WAVE' + body
        yield 'cue_count_huge' + ext, add(b'cue ', struct.pack('<I', 0xFFFFFFFF) + b'\0' * 24)
        yield 'cue_beyond' + ext, add(b'cue ', struct.pack('<I', 1) + struct.pack('<6I', 0, 0, 0, 0, 0, 0x7FFFFFFF))
        yield 'strc_header_huge' + ext, add(b'strc', struct.pack('<II', 0xFFFFFFF0, 5) + b'\0' * 40)
        yield 'strc_count_huge' + ext, add(b'strc', struct.pack('<II', 28, 0x7FFFFFFF) + b'\0' * 40)
        for label, ls, le in (('loop_reversed', 30000, 10), ('loop_beyond', 10, 0x7FFFFFF0), ('loop_neg', 0xFFFFFFF0, 0xFFFFFFFF)):
            yield label + ext, add(b'smpl', struct.pack('<9I', 0, 0, 22675, 60, 0, 0, 0, 1, 0) + struct.pack('<6I', 0, 0, ls, le, 0, 0))
        yield 'smpl_loops_huge' + ext, add(b'smpl', struct.pack('<9I', 0, 0, 22675, 60, 0, 0, 0, 0x7FFFFFFF, 0))


def main(paths):
    rng = random.Random(int(os.environ.get("SEED", "7")))
    work = tempfile.mkdtemp(prefix='cs_corrupt_')
    empty = os.path.join(work, 'params.txt'); open(empty, 'w').close()
    results = {'loaded': 0, 'refused': 0, 'crash': [], 'hang': []}
    for path in paths:
        b = open(path, 'rb').read()
        for vname, data in variants(os.path.basename(path), b, rng):
            f = os.path.join(work, os.path.splitext(os.path.basename(path))[0] + '__' + vname)
            open(f, 'wb').write(data)
            try:
                r = subprocess.run([RENDER, f, os.path.join(work, 'out.wav'), '0.3', empty, '60:0:0.2:100'],
                                   capture_output=True, timeout=60)
                if r.returncode in (0, 1):
                    results['loaded' if r.returncode == 0 else 'refused'] += 1
                else:
                    results['crash'].append((os.path.basename(f), r.returncode))
            except subprocess.TimeoutExpired:
                results['hang'].append(os.path.basename(f))
    print('loaded %(loaded)d, refused %(refused)d' % results)
    print('crashes:', results['crash'])
    print('hangs:', results['hang'])
    return 1 if results['crash'] or results['hang'] else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
