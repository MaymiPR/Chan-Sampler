"""Find where FL stores a setting: the channel events that differ between two saved projects (python -m probe_project_diff a.flp b.flp)."""
import sys, struct; import project_file_lib
def chan0_params(path, eid=215, chan=0):
    h, ev = project_file_lib.read(path); ch = -1; out = {}
    for e, v in ev:
        if e == 64: ch = project_file_lib.val(v)
        if ch == chan and e not in out: out[e] = bytes(v)
    return out
a = chan0_params(sys.argv[1]); b = chan0_params(sys.argv[2])
for e in sorted(set(a) | set(b)):
    if a.get(e) != b.get(e):
        x, y = a.get(e, b''), b.get(e, b'')
        print('event', e, len(x), len(y))
        for o in range(0, max(len(x), len(y)), 4):
            if x[o:o+4] != y[o:o+4]:
                print('  off', o, x[o:o+4].hex(), '->', y[o:o+4].hex(), struct.unpack('<i', y[o:o+4].ljust(4, b'\0'))[0])
