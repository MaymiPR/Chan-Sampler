"""Build a one-note FL project that plays a test sample through channel 0 (Sampler)."""
import os, struct, sys
from lib import flp, paths

TEMPLATE = os.path.join(paths.FL_DIR, 'Data', 'Templates', 'Minimal', 'Basic', 'Basic.flp')
PPQ = 96

def note(pos, length, key=60, velocity=100, channel=0):
    return struct.pack('<IHHIHHBBBBBBBB', pos, 0x4000, channel, length, key, 0,
                       120, 0, 64, 128, 64, velocity, 128, 128)

def playlist_item(pattern, length, pos=0):
    base = 20480
    item = struct.pack('<IHHIHH', pos, base, base + pattern, length, 499, 0)
    item += bytes([120, 0]) + struct.pack('<H', 64) + bytes([64, 100, 128, 128])
    item += struct.pack('<ff', -1.0, -1.0) + bytes(28)
    return item

def point_sampler(ev, sample_path):
    """Channel 0 (FL's Sampler) plays sample_path."""
    ch = -1
    for e in ev:
        if e[0] == 64: ch = flp.val(e[1])
        if e[0] == 196 and ch == 0:
            e[1] = bytearray((sample_path + '\0').encode('utf-16le'))


def add_pattern(ev, notes):
    """Pattern 1, inserted before the first channel, with notes (start_beats,
    length_beats, key, velocity[, channel]), placed on playlist track 1."""
    first_ch = next(i for i, e in enumerate(ev) if e[0] == 64)
    data = b''.join(note(int(n[0] * PPQ), int(n[1] * PPQ), n[2], n[3], n[4] if len(n) > 4 else 0) for n in notes)
    length = int(max(n[0] + n[1] for n in notes) * PPQ)
    ev[first_ch:first_ch] = [[65, bytearray(struct.pack('<H', 1))], [224, bytearray(data)]]
    for e in ev:
        if e[0] == 233:
            e[1] = bytearray(playlist_item(1, length))
            break


def build(out_path, sample_path, patch=None, note_len_beats=8, key=60, velocity=100, notes=None):
    """notes: list of (start_beats, length_beats, key, velocity); default one note."""
    header, ev = flp.read(TEMPLATE)
    point_sampler(ev, sample_path)
    if patch:
        patch(ev)
    add_pattern(ev, notes or [(0, note_len_beats, key, velocity)])
    flp.write(out_path, header, ev)

if __name__ == '__main__':
    build(sys.argv[1], sys.argv[2])
