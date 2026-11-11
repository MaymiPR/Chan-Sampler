"""Render regression: each plug-in's own offline renderer (<Ab>Render, built with the tools) against the golden
renders committed in tests/regress. CI runs it on every push; it catches an unintended DSP change without the
reference plug-in, which CI does not have. The null tests against the originals stay on the dev machine.

    python scripts/render_regress.py --build build                 # compare (exit 1 on any case above its threshold)
    python scripts/render_regress.py --build build --update        # (re)write the goldens after an intended DSP change
    python scripts/render_regress.py --build build --only sine     # cases whose names contain "sine"

tests/regress/jobs.txt (in a suite of several plug-ins tests/regress/<PluginTarget>/jobs.txt each), one case per line:
    <name> signal=<sine|noise|burst|mix|silence> [seconds=0.5] [fs=48000] [block=512] [threshold=-80] [param:Name=text ...]
An instrument's case plays notes instead (instrument mode: notes in, the input is silence unless signal= says otherwise):
    <name> notes=<key:start:length:velocity;...> [sample=<saw|sine|noise|drum|voice>] [seconds=0.5] ... [param:Name=text ...]
notes= reaches the renderer as it is (start and length in seconds, velocity 1 ... 127; the renderers turn it into MIDI
note events). sample= is for a sampler: the script writes that synthesized sample (0.5 s, mono, 32-bit float WAV at
44.1 kHz, root C5) and passes sample=<path> to the renderer, which loads it.
A renderer that takes one case on its command line instead of a job file (the form
"<Ab>Render in.f32 out.f32 channels fs [positional ...] [key=value ...]") is named by a line before the cases:
    @call <Tool> {in} {out} 2 {fs} {args} block={block}
{in} {out} {fs} {block} are filled per case and {args} is the rest of the case line (positional values and keys, in
their order); the tool is run once per case, from the repository root.
signal= seconds= fs= block= threshold= belong to the regression. A renderer key with one of those names goes after
arg: (arg:threshold=-20 reaches the renderer as threshold=-20).
Inputs are synthesized here, deterministically (a fixed-seed generator), so no audio is committed except the
goldens, which are short (0.5 s stereo float32 = 192 KB). The residual is 10 log10(sum((ours - golden)^2) /
sum(golden^2)) in dB; a case fails above its threshold (-80 dB by default: float differences between compilers sit
around -120 dB, a DSP change far above -80). The renderer of a plug-in is found from its plugin.cmake
(foreach(t <Ab>Render ...), or juce_add_console_app(<Ab>Render ...)) under build/<Ab>Render_artefacts/{Release/,}.
A suite of one plug-in (plugin/plugin.cmake) keeps its cases and goldens directly in tests/regress/.
Pure standard library: no numpy needed on the runners.
"""
import argparse, array, glob, math, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGRESS = os.path.join(ROOT, 'tests', 'regress')
PLUGINS = os.path.join(ROOT, 'plugins')
# A suite of one plug-in holds it in plugin/ and its cases directly in tests/regress/; a suite of several holds each
# in plugins/<Id>/ and tests/regress/<Id>/. The folder name states the shape.
PLUGIN = os.path.join(ROOT, 'plugin')
SINGLE = os.path.isfile(os.path.join(PLUGIN, 'plugin.cmake'))


def plugin_dir(target):
    return PLUGIN if SINGLE else os.path.join(PLUGINS, target)


def single_target():
    """The one plug-in's target: juce_add_plugin(<Target> in plugin/plugin.cmake."""
    with open(os.path.join(PLUGIN, 'plugin.cmake'), encoding='utf-8') as h:
        m = re.search(r'juce_add_plugin\(\s*(\w+)', h.read())
    if not m:
        sys.exit('plugin/plugin.cmake: no juce_add_plugin(<Target> line')
    return m.group(1)


def case_dirs():
    """(target, folder) of every plug-in that has regression cases: tests/regress itself, or tests/regress/<Target>."""
    if SINGLE:
        return [(single_target(), REGRESS)] if os.path.isfile(os.path.join(REGRESS, 'jobs.txt')) else []
    return [(os.path.basename(os.path.dirname(jf)), os.path.dirname(jf))
            for jf in sorted(glob.glob(os.path.join(REGRESS, '*', 'jobs.txt')))]


class LCG:
    """Deterministic uniform noise in [-1, 1): the same on every platform and Python."""
    def __init__(self, seed=12345):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        self.s = (1664525 * self.s + 1013904223) & 0xFFFFFFFF
        return self.s / 2147483648.0 - 1.0


def synth(kind, seconds, fs):
    n = int(seconds * fs)
    out = array.array('f')
    rng = LCG(12345)
    for i in range(n):
        t = i / fs
        if kind == 'sine':
            v = 0.25 * math.sin(2 * math.pi * 440.0 * t)
            l, r = v, v
        elif kind == 'noise':
            l, r = 0.25 * rng.next(), 0.25 * rng.next()
        elif kind == 'burst':
            env = 1.0 if (t % 0.125) < 0.02 else 0.0
            v = 0.5 * env * math.sin(2 * math.pi * 220.0 * t)
            l, r = v, v
        elif kind == 'mix':
            v = 0.15 * math.sin(2 * math.pi * 110.0 * t) + 0.1 * math.sin(2 * math.pi * 1234.5 * t) + 0.05 * rng.next()
            v *= 0.6 + 0.4 * math.sin(2 * math.pi * 1.5 * t)
            l, r = v, 0.8 * v + 0.05 * rng.next()
        elif kind == 'silence':
            l, r = 0.0, 0.0
        else:
            sys.exit(f'unknown signal {kind}')
        out.append(l); out.append(r)
    return out


SAMPLE_FS = 44100
SAMPLE_SECONDS = 0.5


def synth_sample(kind):
    """A short mono sample for a sampler to play: root C5, so 261.63 Hz plays at key 60."""
    n = int(SAMPLE_SECONDS * SAMPLE_FS)
    f0 = 261.6256
    rng = LCG(12345)
    out = array.array('f')
    for i in range(n):
        t = i / SAMPLE_FS
        fade = min(1.0, t / 0.005, (SAMPLE_SECONDS - t) / 0.01)
        if kind == 'sine':
            v = 0.5 * math.sin(2 * math.pi * f0 * t)
        elif kind == 'saw':   # eight harmonics, so nothing aliases at the highest key a case plays
            v = 0.4 * sum(math.sin(2 * math.pi * f0 * k * t) / k for k in range(1, 9))
        elif kind == 'noise':
            v = 0.3 * rng.next()
        elif kind == 'drum':
            v = 0.8 * math.exp(-t / 0.06) * math.sin(2 * math.pi * (60.0 + 140.0 * math.exp(-t / 0.02)) * t) \
                + 0.2 * math.exp(-t / 0.01) * rng.next()
        elif kind == 'voice':  # a vowel-like tone with vibrato and a pause: material for stretch modes
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
    if n == 0:
        return float('inf')
    num = 0.0; den = 0.0
    for i in range(n):
        d = ours[i] - gold[i]
        num += d * d
        den += gold[i] * gold[i]
    if den == 0.0:
        return -200.0 if num == 0.0 else float('inf')
    if num == 0.0:
        return -200.0
    return 10.0 * math.log10(num / den)


def find_tool(name, build):
    """A built executable: JUCE's <name>_artefacts/{Release/,} or a plain add_executable's build/{Release/,}."""
    for d in (os.path.join(build, f'{name}_artefacts', 'Release'), os.path.join(build, f'{name}_artefacts'),
              os.path.join(build, 'Release'), build):
        for p in (os.path.join(d, name + '.exe'), os.path.join(d, name)):
            if os.path.isfile(p):
                return p
    # anywhere else under the build folder (a plug-in add_subdirectory()d from plugins/<Id> builds under build/plugins/<Id>)
    for ext in ('.exe', ''):
        hits = sorted(h for h in glob.glob(os.path.join(build, '**', name + ext), recursive=True)
                      if os.path.isfile(h) and 'CMakeFiles' not in h)
        if hits:
            return hits[0]
    sys.exit(f'{name} not built under {build} (configure with the tools on and build the target {name})')


def renderer_for(target, build):
    texts = []
    for cm in (os.path.join(plugin_dir(target), 'plugin.cmake'), os.path.join(plugin_dir(target), 'CMakeLists.txt')):
        if os.path.isfile(cm):
            with open(cm, encoding='utf-8') as h:
                texts.append(h.read())
    text = ''.join(texts)
    m = (re.search(r'foreach\(t\s+(\w+)Render\s', text)
         or re.search(r'(?:juce_add_console_app|add_executable)\((\w+)Render\b', text))
    if not m:
        with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as h:
            m = re.search(r'(?:juce_add_console_app|add_executable)\((' + re.escape(target) + r')Render\b', h.read())
    if not m:
        sys.exit(f'{os.path.relpath(plugin_dir(target), ROOT)}: no "foreach(t <Ab>Render ...", "juce_add_console_app(<Ab>Render" or '
                 f'"add_executable(<Ab>Render" line, cannot find the renderer (or name it with an @call line in jobs.txt)')
    return find_tool(m.group(1) + 'Render', build)


def parse_jobs(path):
    """The cases, and the @call line's words (None: the renderer takes a job file)."""
    cases = []; call = None
    with open(path, encoding='utf-8') as h:
        for line in h:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if line.startswith('@call '):
                call = line.split()[1:]
                continue
            parts = line.split()
            c = {'name': parts[0], 'signal': None, 'seconds': 0.5, 'fs': 48000, 'block': 512, 'threshold': -80.0,
                 'params': [], 'notes': None, 'sample': None}
            for p in parts[1:]:
                if p.startswith('arg:'):          # the renderer's own key, even when its name is one of ours
                    c['params'].append(p[4:]); continue
                k, _, v = p.partition('=')
                if k == 'signal': c['signal'] = v
                elif k == 'notes': c['notes'] = v; c['params'].append(p)
                elif k == 'sample': c['sample'] = v
                elif k == 'seconds': c['seconds'] = float(v)
                elif k == 'fs': c['fs'] = int(v)
                elif k == 'block': c['block'] = int(v)
                elif k == 'threshold': c['threshold'] = float(v)
                else: c['params'].append(p)
            # instrument mode: notes in, silence through the input unless the case names a signal
            c['signal'] = c['signal'] or ('silence' if c['notes'] else 'sine')
            cases.append(c)
    return cases, call


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', default=os.path.join(ROOT, 'build'))
    ap.add_argument('--update', action='store_true', help='write the goldens from the current renders')
    ap.add_argument('--only', default=None, help='cases whose names contain this text')
    a = ap.parse_args()
    build = os.path.abspath(a.build)
    work = os.path.join(build, 'regress')
    rel = lambda path: os.path.relpath(path, ROOT).replace(os.sep, '/')
    os.makedirs(work, exist_ok=True)
    dirs = case_dirs()
    if not dirs:
        print('no tests/regress/jobs.txt or tests/regress/<Plugin>/jobs.txt: nothing to check')
        return
    failed = []; total = 0
    for target, folder in dirs:
        cases, call = parse_jobs(os.path.join(folder, 'jobs.txt'))
        cases = [c for c in cases if not a.only or a.only in c['name']]
        if not cases:
            continue
        tool = find_tool(call[0], build) if call else renderer_for(target, build)
        golden_dir = os.path.join(folder, 'golden')
        os.makedirs(golden_dir, exist_ok=True)
        for c in cases:
            if c['sample']:               # a sampler's case: the synthesized sample, by path
                wav = os.path.join(work, f'sample_{c["sample"]}.wav')
                if not os.path.isfile(wav):
                    write_wav_float(wav, synth_sample(c['sample']))
                c['params'].append('sample=' + rel(wav))
        if call:
            # one renderer run per case, its command line filled from the @call line
            for c in cases:
                inp = os.path.join(work, f'{c["signal"]}_{c["fs"]}_{c["seconds"]}.in.f32')
                if not os.path.isfile(inp):
                    write_f32(inp, synth(c['signal'], c['seconds'], c['fs']))
                c['out'] = os.path.join(work, f'{target}_{c["name"]}.out.f32')
                args = []
                for w in call[1:]:
                    if w == '{args}':
                        args += c['params']
                    else:
                        args.append(w.replace('{in}', rel(inp)).replace('{out}', rel(c['out']))
                                     .replace('{fs}', str(c['fs'])).replace('{block}', str(c['block'])))
                r = subprocess.run([tool] + args, capture_output=True, text=True, cwd=ROOT)
                if r.returncode != 0:
                    sys.exit(f'{os.path.basename(tool)} failed on {target}/{c["name"]} ({" ".join(args)}):\n{r.stdout}\n{r.stderr}')
        # one renderer run per (fs, block): the renderer takes them on its command line
        groups = {}
        for c in cases if not call else []:
            groups.setdefault((c['fs'], c['block']), []).append(c)
        for (fs, block), group in groups.items():
            jobs = os.path.join(work, f'{target}_{fs}_{block}.txt')
            with open(jobs, 'w', encoding='utf-8', newline='\n') as h:
                for c in group:
                    inp = os.path.join(work, f'{c["signal"]}_{fs}_{c["seconds"]}.in.f32')
                    if not os.path.isfile(inp):
                        write_f32(inp, synth(c['signal'], c['seconds'], fs))
                    out = os.path.join(work, f'{target}_{c["name"]}.out.f32')
                    c['out'] = out
                    # Relative to the repository root (the renderer's cwd below), with forward slashes: the renderers
                    # split a job line on spaces, so an absolute path under a folder with a space in its name breaks it.
                    h.write(' '.join([rel(inp), rel(out)] + c['params']) + '\n')
            r = subprocess.run([tool, jobs, f'fs={fs}', f'block={block}'], capture_output=True, text=True, cwd=ROOT)
            if r.returncode != 0:
                sys.exit(f'{os.path.basename(tool)} failed on {jobs}:\n{r.stdout}\n{r.stderr}')
        for c in cases:
            total += 1
            if not os.path.isfile(c['out']):
                sys.exit(f'{target}/{c["name"]}: the renderer wrote no output (check its job line in {work}; a case name or\n'
                         f'parameter with a space breaks the space-separated job format)')
            ours = read_f32(c['out'])
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
        print(f'{total} goldens written under tests/regress; commit them with the DSP change that made them')
        return
    print(f'render regression: {total - len(failed)} of {total} cases within threshold' + (f', FAILED: {", ".join(failed)}' if failed else ''))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
