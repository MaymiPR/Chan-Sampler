"""Where FL's Slice map starts a slice, measured on signals built to tell the
rules apart (round 16; the rule itself is in docs/EVIDENCE.md):

  channels  a stereo file whose left, right and mid (L + R) sign changes fall
            at different places before each hit: which channel(s) FL tests
            when it moves a detected slice start back to a zero crossing
  cue       a sine with cue points (FL's "regions") placed off its zero
            crossings: are file markers moved too?
  strc      the same with ACID slice markers ("Load slice markers" on)

At MUL x2 a slice plays at its own speed from floor(start x 2), with silence
before it, so its first sample in FL's render gives the start exactly (in
each output channel). Ours is measured the same way (ChanSamplerRender).

    python -m probes.slice_starts           # FL renders cached in renders/"""
import os, struct, subprocess, tempfile
import numpy as np
from lib import flrender as F, paths, wavio

SR = 44100
PROBE = os.path.join(paths.ROOT, 'probe'); os.makedirs(PROBE, exist_ok=True)
SLICE_MAP = [F.params(108, '<i', 4), F.params(100, '<i', 0), F.params(104, '<i', 10000)]   # Slice map, PITCH 0, MUL x2
SLICES_FLAG = F.dword(143, 0x0B | 4)   # FL's default sample flags plus Load slice markers

# A quiet background of constant level in each channel whose sign flips to
# negative 2000 samples before each hit and back to positive shortly before
# it, at different places in the two channels (a level step would itself be
# a transient to FL). The louder channel decides the sign of L + R between
# the flips: one file has the left channel louder, the other the right.
A = 0.004
FLIPS = [(30, 120), (120, 30)]   # (left, right) samples before the hit, alternating
HITS = [22050 * (i + 1) for i in range(8)]
PERIOD = 441   # the marker tests' sine (100 Hz), half a sample off so it is never exactly 0
MARK_OFFSETS = [3, 25, 100, 200, 223, 260, 400]   # into a cycle: the first 221 samples are positive


def riff(chunks):
    body = b'WAVE' + b''.join(cid + struct.pack('<I', len(d)) + d + (b'\0' if len(d) & 1 else b'') for cid, d in chunks)
    return b'RIFF' + struct.pack('<I', len(body)) + body


def write_wav(path, x, before=(), after=()):
    x = np.asarray(x, np.float32)
    fmt = struct.pack('<HHIIHH', 3, x.shape[1], SR, SR * x.shape[1] * 4, x.shape[1] * 4, 32)
    open(path, 'wb').write(riff([(b'fmt ', fmt), *before, (b'data', x.tobytes()), *after]))


def channels_signal(louder):
    n = HITS[-1] + 22050
    level = {'left': (2 * A, A), 'right': (A, 2 * A), 'silent right': (A, 0.0)}[louder]   # silent: digital zeros
    x = np.tile(np.array(level), (n, 1))
    for i, p in enumerate(HITS):
        for c, d in enumerate(FLIPS[i % 2]):
            x[p - 2000:p - d, c] *= -1
        u = np.arange(n - p)
        hit = 0.8 * np.exp(-u / 2500) * (0.7 + 0.3 * np.cos(2 * np.pi * u / 150))
        x[p:, 0] += hit
        if louder != 'silent right':
            x[p:, 1] += hit
    path = os.path.join(PROBE, f'slz_{louder.replace(" ", "_")}.wav')
    write_wav(path, x)
    return path


def candidates(i, louder):
    """Each rule's slice start for hit i."""
    p = HITS[i]; dl, dr = FLIPS[i % 2]
    return {'left': p - dl, 'right': p - dr, 'mid': p - (dl if louder == 'left' else dr),
            'either': p - min(dl, dr), 'both': p - max(dl, dr), 'none': p}


def marks():
    return [PERIOD * (30 * (k + 1)) + off for k, off in enumerate(MARK_OFFSETS)]


def marker_signal(kind):
    n = PERIOD * 30 * (len(MARK_OFFSETS) + 2)
    t = np.arange(n) + 0.5
    s = 0.3 * np.sin(2 * np.pi * t / PERIOD)
    x = np.stack([s, s], 1)
    m = marks()
    if kind == 'cue':
        chunk = (b'cue ', struct.pack('<I', len(m)) + b''.join(struct.pack('<II4sIII', i + 1, p, b'data', 0, 0, p) for i, p in enumerate(m)))
    else:   # strc laid out as in signals/strc_tone.wav (which FL reads): header, a type-2 entry, one at 0, the markers
        entries = [(2, 0), (0, 0)] + [(0, p) for p in m]
        chunk = (b'strc', struct.pack('<7I', 28, len(entries), 0, 0, 1, 10, 0)
                 + b''.join(struct.pack('<6I', t, p, 0, 0, 0, 0) for t, p in entries))
    path = os.path.join(PROBE, f'slz_{kind}.wav')
    write_wav(path, x, [chunk] if kind == 'cue' else [], [chunk] if kind == 'strc' else [])
    return path


def marker_candidates(m):
    cyc = m - m % PERIOD
    snapped = cyc if m % PERIOD < 221 else cyc + 221   # back to the first sample of its half-cycle
    return {'snapped': snapped, 'exact': m}


def starts(y, near):
    """Per output channel, the slice start whose output begins after silence
    nearest to 2 x near."""
    out = []
    for c in range(y.shape[1]):
        nz = np.flatnonzero(y[:, c] != 0)
        if len(nz) == 0:   # a silent channel
            continue
        begins = nz[np.r_[True, np.diff(nz) > 1]]   # first sample of each sounding run
        b = begins[np.argmin(np.abs(begins - 2 * near))]
        out.append(b / 2)
    return out


def ours(path, params, seconds):
    tmp = tempfile.mkdtemp()
    pf = os.path.join(tmp, 'p.txt'); open(pf, 'w').write(''.join(f'{k} {v}\n' for k, v in params.items()))
    out = os.path.join(tmp, 'o.wav')
    subprocess.run([paths.RENDER_TOOL, path, out, str(seconds), pf, f'60:0:{seconds - 0.5}:100'], check=True, capture_output=True)
    return wavio.read(out)[0]


def report(name, y, rows):
    print(f'  {name}')
    for label, near, cand in rows:
        got = starts(y, near)
        match = [r for r, v in cand.items() if all(abs(g - v) < 0.6 for g in got)]
        print(f'    {label:14s} ' + '  '.join(f'{v:9.1f}' for v in got) + '   ' + ('= ' + ', '.join(match) if match else
              'no rule: ' + ', '.join(f'{r} {v}' for r, v in cand.items())))


def main():
    for louder in ('left', 'right', 'silent right'):
        ch = channels_signal(louder)
        fl = F.render(f'slz_{louder.replace(" ", "_")}', ch, SLICE_MAP, beats=24)
        us = ours(ch, {'stretchMode': 6, 'stretchMul': 2.0}, len(fl) / SR)
        rows = [(f'hit {i} L-{FLIPS[i % 2][0]} R-{FLIPS[i % 2][1]}', HITS[i], candidates(i, louder)) for i in range(len(HITS))]
        print(f'Zero-crossing channel (Slice map, MUL x2), {louder} channel louder:')
        report('FL', fl, rows)
        report('ours', us, rows)
    for kind, extra, params in (('cue', [], {}), ('strc', [SLICES_FLAG], {'loadSlices': 1})):
        path = marker_signal(kind)
        fl = F.render(f'slz_{kind}', path, SLICE_MAP + extra, beats=24)
        us = ours(path, {'stretchMode': 6, 'stretchMul': 2.0, **params}, len(fl) / SR)
        rows = [(f'mark +{m % PERIOD}', m, marker_candidates(m)) for m in marks()]
        print(f'File markers ({kind}):')
        report('FL', fl, rows)
        report('ours', us, rows)


if __name__ == '__main__':
    main()
