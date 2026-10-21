"""In-FL null cases for features not covered elsewhere: loop playback, echo
ping pong / fat mode / Mod X-Y, velocity and key tracking, mono / porta /
slide, max polyphony, root note and fine tune.

FL 2026 storage (found by saving from FL's GUI and comparing the files):
- loop points: event 251 (43 bytes, only present once set): int count (2),
  int start, 0, int end, 0, double -1.0, int -1, byte 0, int 0x7fffffff x2, 0, 1
- Use loop points: event 143 (sample flags) bit 8; Ping pong loop: event 20 (byte)
- echo ping pong: parameters (215) byte 10 bit 2; fat mode: bit 4
- echo Mod Y / X: event 138 bytes 0 / 2 (centre 128; knob % = (raw - 128) / 128;
  checked by sound: byte 2 lowers the echoes' cutoff when below 128, byte 0 raises resonance above 128)
- tracking: event 228 x2 (velocity, then key): mid, pan, mod X, mod Y (int32)
- polyphony: event 221: max (u32), slide (u32), flags (byte: 1 mono, 2 porta)
- root note: event 135 (u32, only written when not 60; PyFLP ChannelID.RootNote);
  fine tune: levels (219) pitch field (cents)
"""
import os, shutil, struct, sys
from lib import flp, flrender as F, nullfl, paths

def loop_points(start, end):
    payload = (struct.pack('<5i', 2, start, 0, end, 0) + struct.pack('<d', -1.0) + struct.pack('<i', -1)
               + b'\x00' + struct.pack('<ii', 0x7fffffff, 0x7fffffff) + b'\x00\x01')
    assert len(payload) == 43
    def p(ev):
        idx = None
        for i, e in F._channel0(ev):
            if e[0] == 215: idx = i
        ev.insert(idx + 1, [251, bytearray(payload)])
    return p

def sample_flags(use_loop):
    def p(ev):
        for _, e in F._channel0(ev):
            if e[0] == 143:
                v = struct.unpack('<I', bytes(e[1]))[0]
                v = (v | 8) if use_loop else (v & ~8)
                e[1] = bytearray(struct.pack('<I', v)); return
        raise KeyError(143)
    return p

def param_byte_bits(offset, set_bits):
    def p(ev):
        for _, e in F._channel0(ev):
            if e[0] == 215:
                e[1][offset] |= set_bits; return
    return p

def echo_on(feed=12800, count=4, time=144):
    def p(ev):
        for _, e in F._channel0(ev):
            if e[0] == 209: struct.pack_into('<5I', e[1], 0, feed, 6400, 0, count, time); return
    return p

def echo_pan(pan):
    def p(ev):
        for _, e in F._channel0(ev):
            if e[0] == 209: struct.pack_into('<I', e[1], 4, pan); return
    return p

def ev_bytes(eid, offset, value):
    def p(ev):
        for _, e in F._channel0(ev):
            if e[0] == eid: e[1][offset] = value; return
        raise KeyError(eid)
    return p

N = 88200
CASES = []
# Loop playback (noise, 3 s note on a 2 s sample)
for name, s, e, extra_fl, extra_ours in (
        ('u_loop_fwd', 22050, 66150, [], {}),
        ('u_loop_short', 30000, 31000, [], {}),
        ('u_loop_pingpong', 22050, 66150, [F.byte(20, 1)], {'pingPong': 1}),
        ('u_loop_xfade', 22050, 66150, [F.params(84, '<I', 256)], {'pcCrossfade': 256 / 1024}),
        ('u_loop_off', 22050, 66150, [sample_flags(False)], {'useLoop': 0})):
    fl = [loop_points(s, e), sample_flags(True)] + extra_fl
    ours = {'useLoop': 1, 'loopStart': s / N, 'loopEnd': e / N, **extra_ours}
    if name == 'u_loop_off':
        fl = [loop_points(s, e), sample_flags(False)]
    CASES.append(dict(name=name, sample='signals/noise.wav', fl=fl, ours=ours, beats=7))

# Echo switches and Mod X / Y
CASES += [
    dict(name='u_echo_ping', sample='signals/decay.wav', fl=[echo_on(), param_byte_bits(10, 2)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoPing': 1}, beats=2),
    dict(name='u_echo_fat', sample='signals/decay.wav', fl=[echo_on(), param_byte_bits(10, 4)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoFat': 1}, beats=2),
    dict(name='u_echo_fat_c2f3200', sample='signals/decay.wav', fl=[echo_on(feed=3200, count=2), param_byte_bits(10, 4)], ours={'echoFeed': 3200 / 25600, 'echoCount': 2, 'echoTime': 3, 'echoFat': 1}, beats=2),
    dict(name='u_echo_fat_f19200', sample='signals/decay.wav', fl=[echo_on(feed=19200), param_byte_bits(10, 4)], ours={'echoFeed': 19200 / 25600, 'echoCount': 4, 'echoTime': 3, 'echoFat': 1}, beats=2),
    dict(name='u_echo_fat_pan3200', sample='signals/decay.wav', fl=[echo_on(), echo_pan(3200), param_byte_bits(10, 4)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoFat': 1, 'echoPan': -0.5}, beats=2),
    dict(name='u_echo_fat_ping', sample='signals/decay.wav', fl=[echo_on(), param_byte_bits(10, 6)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoFat': 1, 'echoPing': 1}, beats=2),
    dict(name='u_echo_modx-50', sample='signals/noise.wav', fl=[echo_on(), ev_bytes(138, 2, 64)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoX': -0.5}, beats=2),
    dict(name='u_echo_modx-25', sample='signals/noise.wav', fl=[echo_on(), ev_bytes(138, 2, 96)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoX': -0.25}, beats=2),
    dict(name='u_echo_modx+50_cut', sample='signals/noise.wav', fl=[echo_on(), ev_bytes(138, 2, 192), F.levels('mod_x', 128)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoX': 0.5, 'cutoff': 0.5}, beats=2),
    dict(name='u_echo_modx+25_cut', sample='signals/noise.wav', fl=[echo_on(), ev_bytes(138, 2, 160), F.levels('mod_x', 64)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoX': 0.25, 'cutoff': 0.25}, beats=2),
    dict(name='u_echo_mody-25_res', sample='signals/noise.wav', fl=[echo_on(), ev_bytes(138, 0, 96), F.levels('mod_y', 128)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoY': -0.25, 'resonance': 0.5}, beats=2),
    dict(name='u_echo_mody+50', sample='signals/noise.wav', fl=[echo_on(), ev_bytes(138, 0, 192)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoY': 0.5}, beats=2),
    dict(name='u_echo_mody-50_res', sample='signals/noise.wav', fl=[echo_on(), ev_bytes(138, 0, 64), F.levels('mod_y', 128)], ours={'echoFeed': 0.5, 'echoCount': 4, 'echoTime': 3, 'echoY': -0.5, 'resonance': 0.5}, beats=2),
]

# Velocity / key tracking (event 228: mid, pan, mod x, mod y)
def tracking(which, mid, pan=0, modx=0, mody=0):
    return F.struct_field(228, 0, '<4i', (mid, pan, modx, mody), 0 if which == 'vel' else 1) if False else _track(which, mid, pan, modx, mody)
def _track(which, mid, pan, modx, mody):
    def p(ev):
        n = 0
        for _, e in F._channel0(ev):
            if e[0] == 228:
                if n == (0 if which == 'vel' else 1):
                    struct.pack_into('<4i', e[1], 0, mid, pan, modx, mody); return
                n += 1
        raise KeyError(228)
    return p
CASES += [
    dict(name='u_velpan', sample='signals/noise.wav', fl=[_track('vel', 100, 128, 0, 0)], ours={'velPan': 0.5}, vel=40, beats=2),
    dict(name='u_velmodx', sample='signals/noise.wav', fl=[_track('vel', 100, 0, -128, 0)], ours={'velX': -0.5}, vel=40, beats=2),
    dict(name='u_keypan', sample='signals/noise.wav', fl=[_track('key', 60, 128, 0, 0)], ours={'keyPan': 0.5}, notes=[(0, 2, 72, 100)], beats=2),
    dict(name='u_keymodx', sample='signals/noise.wav', fl=[_track('key', 60, 0, -128, 0)], ours={'keyX': -0.5}, notes=[(0, 2, 84, 100)], beats=2),
    dict(name='u_velmid', sample='signals/noise.wav', fl=[_track('vel', 64, 128, 0, 0)], ours={'velPan': 0.5, 'velMid': 64 / 128}, vel=100, beats=2),

    dict(name='u_velpan_v127', sample='signals/noise.wav', fl=[_track('vel', 100, 128, 0, 0)], ours={'velPan': 0.5}, vel=127, beats=2),
    dict(name='u_velmid128', sample='signals/noise.wav', fl=[_track('vel', 128, 128, 0, 0)], ours={'velPan': 0.5, 'velMid': 1.0}, vel=100, beats=2),
    dict(name='u_velmid32', sample='signals/noise.wav', fl=[_track('vel', 32, 128, 0, 0)], ours={'velPan': 0.5, 'velMid': 32 / 128}, vel=100, beats=2),
    dict(name='u_velx_v127', sample='signals/noise.wav', fl=[_track('vel', 100, 0, -128, 0)], ours={'velX': -0.5}, vel=127, beats=2),
    dict(name='u_velx_v70', sample='signals/noise.wav', fl=[_track('vel', 100, 0, 128, 0)], ours={'velX': 0.5}, vel=70, beats=2),
    dict(name='u_keypan_k66', sample='signals/noise.wav', fl=[_track('key', 60, 128, 0, 0)], ours={'keyPan': 0.5}, notes=[(0, 2, 66, 100)], beats=2),
    dict(name='u_keypan_a64', sample='signals/noise.wav', fl=[_track('key', 60, 64, 0, 0)], ours={'keyPan': 0.25}, notes=[(0, 2, 72, 100)], beats=2),
    dict(name='u_keyx_k66', sample='signals/noise.wav', fl=[_track('key', 60, 0, -128, 0)], ours={'keyX': -0.5}, notes=[(0, 2, 66, 100)], beats=2),
    dict(name='u_keyy_k72', sample='signals/noise.wav', fl=[_track('key', 60, 0, 0, 64), F.levels('mod_x', 160)], ours={'keyY': 0.25, 'cutoff': 160 / 256}, notes=[(0, 2, 72, 100)], beats=2),

    dict(name='u_cut0', sample='signals/noise.wav', fl=[F.levels('mod_x', 0)], ours={'cutoff': 0}, beats=2),
    # (u_cut2 is ~-25 dB only because FL's render is 16-bit at this tiny level)
    dict(name='u_cut2', sample='signals/noise.wav', fl=[F.levels('mod_x', 2)], ours={'cutoff': 2 / 256}, beats=2),]

# Polyphony: max / slide / flags (1 mono, 2 porta)
def poly(maxv=0, slide=500, flags=0):
    def p(ev):
        for _, e in F._channel0(ev):
            if e[0] == 221: e[1] = bytearray(struct.pack('<IIB', maxv, slide, flags)); return
        raise KeyError(221)
    return p
TWO = [(0, 2, 60, 100), (1, 2, 67, 100)]
# Volume envelope on (instant attack, full sustain, short linear release)
_ENV = dict(env_on=1, delay=100, attack=100, hold=100, decay=100, sustain=128, release=3000, att_tension=0, dec_tension=0, rel_tension=0)
ENV = [F.env('vol', k, v) for k, v in _ENV.items()]
def _envpos(raw): return max(0, raw - 100) / 65436
ENV_OURS = {'volEnvOn': 1, 'volDel': 0, 'volAtt': 0, 'volHold': 0, 'volDec': 0, 'volSus': 1.0, 'volRel': _envpos(3000),
            'volAttT': 0, 'volDecT': 0, 'volRelT': 0}
CASES += [
    dict(name='u_mono', sample='signals/sine.wav', fl=[poly(flags=1)], ours={'mono': 1, 'slide': 500 / 1660}, notes=TWO, beats=4),
    dict(name='u_porta', sample='signals/sine.wav', fl=[poly(flags=2)], ours={'porta': 1, 'slide': 500 / 1660}, notes=TWO, beats=4),
    dict(name='u_porta_slide1000', sample='signals/sine.wav', fl=[poly(flags=2, slide=1000)], ours={'porta': 1, 'slide': 1000 / 1660}, notes=TWO, beats=4),
    dict(name='u_mono_porta', sample='signals/sine.wav', fl=[poly(flags=3)], ours={'mono': 1, 'porta': 1, 'slide': 500 / 1660}, notes=TWO, beats=4),
    dict(name='u_mono_s0', sample='signals/sine.wav', fl=[poly(flags=1, slide=0)], ours={'mono': 1, 'slide': 0}, notes=TWO, beats=4),
    dict(name='u_mono_s250', sample='signals/sine.wav', fl=[poly(flags=1, slide=250)], ours={'mono': 1, 'slide': 250 / 1660}, notes=TWO, beats=4),
    dict(name='u_mono_s1000', sample='signals/sine.wav', fl=[poly(flags=1, slide=1000)], ours={'mono': 1, 'slide': 1000 / 1660}, notes=TWO, beats=4),
    dict(name='u_mono_s1400', sample='signals/sine.wav', fl=[poly(flags=1, slide=1400)], ours={'mono': 1, 'slide': 1400 / 1660}, notes=TWO, beats=4),
    dict(name='u_mono_held', sample='signals/sine.wav', fl=[poly(flags=1)], ours={'mono': 1, 'slide': 500 / 1660}, notes=[(0, 3, 60, 100), (1, 1, 67, 100)], beats=4),
    dict(name='u_monoenv_two', sample='signals/sine.wav', fl=[poly(flags=1)] + ENV, ours={'mono': 1, 'slide': 500 / 1660, **ENV_OURS}, notes=TWO, beats=4),
    dict(name='u_monoenv_held', sample='signals/sine.wav', fl=[poly(flags=1)] + ENV, ours={'mono': 1, 'slide': 500 / 1660, **ENV_OURS}, notes=[(0, 3, 60, 100), (1, 1, 67, 100)], beats=4),
    dict(name='u_monoenv_3', sample='signals/sine.wav', fl=[poly(flags=1)] + ENV, ours={'mono': 1, 'slide': 500 / 1660, **ENV_OURS}, notes=[(0, 1.5, 60, 100), (0.5, 0.25, 64, 100), (1, 1, 67, 100)], beats=4),
    # glide steps sit on the song's tick grid: releases and note-ons at ticks a fraction of a sample apart
    *[dict(name=f'u_monorel_{L}', sample='signals/sine.wav', fl=[poly(flags=1)] + ENV, ours={'mono': 1, 'slide': 500 / 1660, **ENV_OURS}, notes=[(0, 4, 60, 100), (1, L, 67, 100)], beats=5) for L in (0.25, 0.5, 0.75, 1.25)],
    *[dict(name=f'u_monoon_{st}', sample='signals/sine.wav', fl=[poly(flags=1)] + ENV, ours={'mono': 1, 'slide': 500 / 1660, **ENV_OURS}, notes=[(0, 4, 60, 100), (st, 1, 67, 100)], beats=5) for st in (0.25, 0.75)],
    # FL's renders ignore Max polyphony (raw 1 and 2, with and without the
    # volume envelope, same and different keys): every voice plays.
    dict(name='u_maxpoly1', sample='signals/noise.wav', fl=[poly(maxv=1)], ours={'maxPoly': 0}, notes=TWO, beats=4),
    dict(name='u_maxpoly1_env', sample='signals/noise.wav', fl=[poly(maxv=1)] + ENV, ours={'maxPoly': 0, **ENV_OURS}, notes=[(0, 3, 60, 100), (1, 2, 64, 100), (1, 2, 67, 100)], beats=4),
    # Overlapping notes on one key: the first note-off ends the older voice only
    dict(name='u_samekey', sample='signals/noise.wav', fl=ENV, ours=ENV_OURS, notes=[(0, 2, 60, 100), (1, 2, 60, 100)], beats=4),
]

# Top velocities: FL hands a plugin MIDI 127 for both 127 and 128 (100%)
CASES += [
    dict(name='u_vel128', sample='signals/noise.wav', vel=128, beats=2),
    dict(name='u_vel127', sample='signals/noise.wav', vel=127, beats=2),
]

# Root note and fine tune
def root_note(n):
    def p(ev):
        for i, e in F._channel0(ev):
            if e[0] == 135: e[1] = bytearray(struct.pack('<I', n)); return
        idx = None
        for i, e in F._channel0(ev):
            if e[0] == 215: idx = i
        ev.insert(idx + 1, [135, bytearray(struct.pack('<I', n))])
    return p
CASES += [
    dict(name='u_root57', sample='signals/sine.wav', fl=[root_note(57)], ours={'rootNote': 57}, beats=2),
    dict(name='u_root72', sample='signals/sine.wav', fl=[root_note(72)], ours={'rootNote': 72}, notes=[(0, 2, 67, 100)], beats=2),
    dict(name='u_fine30', sample='signals/sine.wav', fl=[F.levels('pitch', 30)], ours={'fineTune': 30}, beats=2),
    dict(name='u_fine-75', sample='signals/sine.wav', fl=[F.levels('pitch', -75)], ours={'fineTune': -75}, beats=2),
]

# Compressed formats, using samples that ship with FL Studio (copied in when
# missing; not in the repository). Long samples need the envelope so the note
# stops before the other half of the render.
_FLD = r'C:\Program Files\Image-Line\FL Studio 2026\Data\Patches\Misc\Used by demo projects'
for _name, _src in (('fmt_test.flac', r'Astes - Bien Duro\WAEC_Clap_06.flac'),
                    ('fmt_test.ogg', r'Astes - Bien Duro\1.1 Build #1.ogg'),
                    ('fmt_test.mp3', r'Gimbal & Sinan + Futorial - RawFL\Sincinaty_RawFL_Sample_03.mp3')):
    _dst = os.path.join(paths.ROOT, _name)
    if not os.path.exists(_dst) and os.path.exists(os.path.join(_FLD, _src)):
        shutil.copyfile(os.path.join(_FLD, _src), _dst)
CASES += [
    dict(name='f_flac', sample='fmt_test.flac', beats=4),
    dict(name='f_ogg', sample='fmt_test.ogg', beats=4),
    dict(name='f_mp3env', sample='fmt_test.mp3', fl=ENV, ours=ENV_OURS, beats=4),
]

# Notes after the first, and glides on the song's tick grid, need both halves
# at the same sub-sample positions (a 16-beat gap shifts them by a sample).
for c in CASES:
    if len(c.get('notes') or []) > 1:
        c['gap'] = 26

if __name__ == '__main__':
    nullfl.run(CASES, [a for a in sys.argv[1:] if a != '-f'] or None, force='-f' in sys.argv)
