"""Robustness checks: host sample rates, mono / 16 / 24-bit / 48 kHz files and
a very long file, through ChanSamplerRender. Reports pitch, level, duration
and timing."""
import os, sys, struct, subprocess, time, numpy as np; from lib import wavio
from lib import paths
HERE = paths.ROOT
R = paths.RENDER_TOOL
out = sys.argv[1]

def write_pcm(path, x, sr, bits):
    ch = x.shape[1]; scale = 2 ** (bits - 1) - 1
    q = np.clip(np.round(x * scale), -scale - 1, scale).astype(np.int64)
    if bits == 16: data = q.astype('<i2').tobytes()
    else: data = b''.join(int(v).to_bytes(3, 'little', signed=True) for v in q.ravel())
    fmt = struct.pack('<HHIIHH', 1, ch, sr, sr * ch * bits // 8, ch * bits // 8, bits)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(data)) + data
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)

def sine(sr, secs, ch):
    t = np.arange(int(sr * secs)) / sr
    return np.repeat((0.5 * np.sin(2 * np.pi * 440 * t))[:, None], ch, 1)

files = {}
files['stereo float 44.1k'] = os.path.join(HERE, 'signals/sine.wav')
p = os.path.join(out, 'mono16.wav'); write_pcm(p, sine(44100, 2, 1), 44100, 16); files['mono 16-bit 44.1k'] = p
p = os.path.join(out, 'st24.wav'); write_pcm(p, sine(44100, 2, 2), 44100, 24); files['stereo 24-bit 44.1k'] = p
p = os.path.join(out, 'st48.wav'); wavio.write_float(p, sine(48000, 2, 2).astype(np.float32), 48000); files['stereo float 48k'] = p

def freq(y, sr):
    s = y[int(0.2 * sr):int(1.2 * sr)]
    f = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 20)); k = np.argmax(f)
    return k * sr / (1 << 20)

pf = os.path.join(out, 'none.txt'); open(pf, 'w').close()
for name, path in files.items():
    for sr in (44100, 48000, 96000):
        o = os.path.join(out, 'r.wav')
        subprocess.run([R, path, o, '2.5', pf, '60:0:2.2:100'], check=True, env=dict(os.environ, RENDER_SR=str(sr)))
        y, osr = wavio.read(o)
        L, Rr = y[:, 0], y[:, 1]
        nz = np.nonzero(np.abs(L) > 1e-4)[0]
        print(f'{name:22s} host {sr:6d}: out rate {osr}, pitch {freq(L, osr):7.2f} Hz, level {np.abs(L).max():.3f}, '
              f'L=R {np.allclose(L, Rr, atol=1e-6)}, sounds {nz[-1] / osr if len(nz) else 0:.2f} s', flush=True)

# very long file: 10 minutes stereo float
p = os.path.join(out, 'long10min.wav')
if not os.path.exists(p):
    rng = np.random.default_rng(0); wavio.write_float(p, (rng.standard_normal((44100 * 600, 2)) * 0.1).astype(np.float32), 44100)
t = time.time()
subprocess.run([R, p, os.path.join(out, 'rl.wav'), '3', pf, '60:0:2.5:100'], check=True)
print(f'10-minute file: load + precompute + 3 s render took {time.time() - t:.1f} s', flush=True)
