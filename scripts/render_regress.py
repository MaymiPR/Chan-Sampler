"""Render regression: ChanSamplerRender (the whole plug-in, offline) against the golden renders committed in
tests/regress. CI runs it on every push; it catches an unintended change in the sound without FL Studio, which CI
does not have. The null tests against FL (nulltest/) stay on the dev machine.

    python scripts/render_regress.py --build build                 # compare (exit 1 on any case above its threshold)
    python scripts/render_regress.py --build build --update        # (re)write the goldens after an intended change
    python scripts/render_regress.py --build build --only stretch  # cases whose names contain "stretch"

This is Chan Sampler's version of the standard script (an instrument: a sample and notes in, not audio through).
tests/regress/ChanSampler/jobs.txt, one case per line:
    <name> sample=<saw|sine|noise|drum|voice> notes=<key:start:length:velocity;...> [seconds=1.0] [fs=44100]
           [threshold=-80] [factory=<preset name>] [<paramID>=<plain value> ...]
The sample is synthesized here, deterministically (a fixed-seed generator, 0.5 s mono 32-bit float WAV), so no audio
is committed except the goldens (stereo float32). Parameter values are plain (unnormalised); a choice takes its index.
The residual is 10 log10(sum((ours - golden)^2) / sum(golden^2)) in dB; a case fails above its threshold (-80 dB by
default: float differences between compilers sit far below it, a change in the sound far above).
Pure standard library: no numpy needed on the runners.
"""
import argparse, array, glob, math, os, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGRESS = os.path.join(ROOT, 'tests', 'regress')
SAMPLE_FS = 44100
SAMPLE_SECONDS = 0.5


class LCG:
    """Deterministic uniform noise in [-1, 1): the same on every platform and Python."""
    def __init__(self, seed=12345):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        self.s = (1664525 * self.s + 1013904223) & 0xFFFFFFFF
        return self.s / 2147483648.0 - 1.0


def synth(kind):
    """A short mono sample: what the sampler plays. Root C5 (the renderer sets it), so 261.63 Hz plays at key 60."""
    n = int(SAMPLE_SECONDS * SAMPLE_FS)
    f0 = 261.6256
    rng = LCG(12345)
    out = array.array('f')
    for i in range(n):
        t = i / SAMPLE_FS
        fade = min(1.0, t / 0.005, (SAMPLE_SECONDS - t) / 0.01)
        if kind == 'sine':
            v = 0.5 * math.sin(2 * math.pi * f0 * t)
        elif kind == 'saw':   # eight harmonics, so nothing aliases at the highest key the cases play
            v = 0.4 * sum(math.sin(2 * math.pi * f0 * k * t) / k for k in range(1, 9))
        elif kind == 'noise':
            v = 0.3 * rng.next()
        elif kind == 'drum':
            v = 0.8 * math.exp(-t / 0.06) * math.sin(2 * math.pi * (60.0 + 140.0 * math.exp(-t / 0.02)) * t) \
                + 0.2 * math.exp(-t / 0.01) * rng.next()
        elif kind == 'voice':  # a vowel-like tone with vibrato and a pause: material for the stretch modes
            vib = 1.0 + 0.01 * math.sin(2 * math.pi * 5.5 * t)
            env = 1.0 if (t % 0.25) < 0.18 else 0.0
            v = env * 0.3 * sum(math.sin(2 * math.pi * f0 * vib * k * t) * (1.0 if k in (2, 3) else 0.4) / k for k in range(1, 7))
        else:
            sys.exit(f'unknown sample {kind}')
        out.append(v * fade)
    return out


def write_wav_float(path, mono):
    b = array.array('f', mono)
    if sys.byteorder != 'little':
        b.byteswap()
    data = b.tobytes()
    with open(path, 'wb') as h:
        h.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
        h.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 3, 1, SAMPLE_FS, SAMPLE_FS * 4, 4, 32))
        h.write(b'data' + struct.pack('<I', len(data)) + data)


def read_wav(path):
    """Interleaved float samples from a WAV (32-bit float, plain or extensible, or 16 / 24 / 32-bit integer)."""
    with open(path, 'rb') as h:
        b = h.read()
    if b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        sys.exit(f'{path}: not a WAV file')
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(b):
        cid, size = b[pos:pos + 4], struct.unpack('<I', b[pos + 4:pos + 8])[0]
        body = b[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            tag, ch, fs, _, _, bits = struct.unpack('<HHIIHH', body[:16])
            if tag == 0xFFFE:
                tag = struct.unpack('<H', body[24:26])[0]
            fmt = (tag, ch, bits)
        elif cid == b'data':
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        sys.exit(f'{path}: no fmt or data chunk')
    tag, ch, bits = fmt
    a = array.array('f')
    if tag == 3 and bits == 32:
        a.frombytes(data[:len(data) // 4 * 4])
        if sys.byteorder != 'little':
            a.byteswap()
    elif tag == 1 and bits in (16, 24, 32):
        w, scale = bits // 8, float(1 << (bits - 1))
        for i in range(0, len(data) - w + 1, w):
            a.append(int.from_bytes(data[i:i + w], 'little', signed=True) / scale)
    else:
        sys.exit(f'{path}: unsupported WAV format {tag} / {bits} bits')
    if ch == 1:
        st = array.array('f')
        for v in a:
            st.append(v); st.append(v)
        a = st
    return a


def read_f32(path):
    a = array.array('f')
    with open(path, 'rb') as h:
        a.frombytes(h.read())
    if sys.byteorder != 'little':
        a.byteswap()
    return a


def write_f32(path, a):
    b = array.array('f', a)
    if sys.byteorder != 'little':
        b.byteswap()
    with open(path, 'wb') as h:
        h.write(b.tobytes())


def residual_db(ours, gold):
    n = min(len(ours), len(gold))
    num = sum((ours[i] - gold[i]) ** 2 for i in range(n))
    den = sum(gold[i] ** 2 for i in range(n))
    if den == 0.0:
        return -200.0 if num == 0.0 else 0.0
    if num == 0.0:
        return -200.0
    return 10.0 * math.log10(num / den)


def renderer(build):
    for p in (os.path.join(build, 'ChanSamplerRender_artefacts', 'Release', 'ChanSamplerRender.exe'),
              os.path.join(build, 'ChanSamplerRender_artefacts', 'Release', 'ChanSamplerRender'),
              os.path.join(build, 'ChanSamplerRender_artefacts', 'ChanSamplerRender.exe'),
              os.path.join(build, 'ChanSamplerRender_artefacts', 'ChanSamplerRender')):
        if os.path.isfile(p):
            return p
    sys.exit(f'ChanSamplerRender not built under {build} (build the target ChanSamplerRender)')


def parse_jobs(path):
    cases = []
    with open(path, encoding='utf-8') as h:
        for line in h:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            c = {'name': parts[0], 'sample': 'saw', 'notes': '60:0:0.4:100', 'seconds': 1.0, 'fs': 44100,
                 'threshold': -80.0, 'factory': None, 'params': []}
            for p in parts[1:]:
                k, _, v = p.partition('=')
                if k in ('sample', 'notes', 'factory'):
                    c[k] = v
                elif k == 'seconds':
                    c['seconds'] = float(v)
                elif k == 'fs':
                    c['fs'] = int(v)
                elif k == 'threshold':
                    c['threshold'] = float(v)
                else:
                    c['params'].append((k, v))
            cases.append(c)
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', default=os.path.join(ROOT, 'build'))
    ap.add_argument('--update', action='store_true', help='write the goldens from the current renders')
    ap.add_argument('--only', default=None, help='cases whose names contain this text')
    a = ap.parse_args()
    build = os.path.abspath(a.build)
    work = os.path.join(build, 'regress')
    os.makedirs(work, exist_ok=True)
    jobfiles = sorted(glob.glob(os.path.join(REGRESS, '*', 'jobs.txt')))
    if not jobfiles:
        print('no tests/regress/<Plugin>/jobs.txt: nothing to check')
        return
    tool = renderer(build)
    failed, total = [], 0
    for jf in jobfiles:
        target = os.path.basename(os.path.dirname(jf))
        golden_dir = os.path.join(REGRESS, target, 'golden')
        os.makedirs(golden_dir, exist_ok=True)
        for c in parse_jobs(jf):
            if a.only and a.only not in c['name']:
                continue
            total += 1
            sample = os.path.join(work, f'sample_{c["sample"]}.wav')
            if not os.path.isfile(sample):
                write_wav_float(sample, synth(c['sample']))
            params = os.path.join(work, f'{target}_{c["name"]}.params.txt')
            with open(params, 'w', encoding='utf-8', newline='\n') as h:
                h.write(''.join(f'{k} {v}\n' for k, v in c['params']))
            out = os.path.join(work, f'{target}_{c["name"]}.out.wav')
            if os.path.isfile(out):
                os.remove(out)
            env = dict(os.environ, RENDER_SR=str(c['fs']), RENDER_BPM='130')
            env.pop('RENDER_FACTORY', None)
            if c['factory']:
                env['RENDER_FACTORY'] = c['factory'].replace('_', ' ')
            r = subprocess.run([tool, sample, out, str(c['seconds']), params, c['notes']], capture_output=True,
                               text=True, cwd=ROOT, env=env)
            if r.returncode != 0 or not os.path.isfile(out):
                sys.exit(f'ChanSamplerRender failed on {target}/{c["name"]} (exit {r.returncode}):\n{r.stdout}\n{r.stderr}')
            ours = read_wav(out)
            gpath = os.path.join(golden_dir, c['name'] + '.f32')
            if a.update:
                write_f32(gpath, ours)
                print(f'  wrote  {target}/{c["name"]} ({len(ours) // 2} frames)')
                continue
            if not os.path.isfile(gpath):
                print(f'  MISSING {target}/{c["name"]}: no golden (run with --update once)')
                failed.append(c['name']); continue
            gold = read_f32(gpath)
            db = residual_db(ours, gold)
            ok = db <= c['threshold'] and len(ours) == len(gold)
            print(f'  {"ok  " if ok else "FAIL"} {target}/{c["name"]:24s} {db:8.1f} dB re golden (threshold {c["threshold"]:.0f}, {len(ours) // 2} frames)')
            if not ok:
                failed.append(c['name'])
    if a.update:
        print(f'{total} goldens written under tests/regress; commit them with the change that made them')
        return
    print(f'render regression: {total - len(failed)} of {total} cases within threshold' + (f', FAILED: {", ".join(failed)}' if failed else ''))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
