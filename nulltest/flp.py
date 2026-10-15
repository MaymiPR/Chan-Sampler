"""Minimal FL Studio project (.flp) event reader/writer for null-test projects.
Event IDs follow PyFLP's reverse-engineered tables (ChannelID, PatternID, ...)."""
import struct
WORD, DWORD, TEXT, DATA = 64, 128, 192, 208

def read(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'FLhd'
    hlen = struct.unpack('<I', b[4:8])[0]
    header = b[8:8 + hlen]
    p = 8 + hlen
    assert b[p:p + 4] == b'FLdt'
    dlen = struct.unpack('<I', b[p + 4:p + 8])[0]
    d = b[p + 8:p + 8 + dlen]
    events, i = [], 0
    while i < len(d):
        eid = d[i]; i += 1
        if eid < WORD:   v = d[i:i + 1]; i += 1
        elif eid < DWORD: v = d[i:i + 2]; i += 2
        elif eid < TEXT: v = d[i:i + 4]; i += 4
        else:
            n = 0; shift = 0
            while True:
                c = d[i]; i += 1
                n |= (c & 0x7F) << shift; shift += 7
                if not c & 0x80: break
            v = d[i:i + n]; i += n
        events.append([eid, bytearray(v)])
    return header, events

def write(path, header, events):
    out = bytearray()
    for eid, v in events:
        out.append(eid)
        if eid >= TEXT:
            n = len(v)
            while True:
                c = n & 0x7F; n >>= 7
                out.append(c | (0x80 if n else 0))
                if not n: break
        out += v
    data = b'FLhd' + struct.pack('<I', len(header)) + header + b'FLdt' + struct.pack('<I', len(out)) + bytes(out)
    open(path, 'wb').write(data)

def val(v):
    return int.from_bytes(v, 'little') if len(v) <= 4 else v
