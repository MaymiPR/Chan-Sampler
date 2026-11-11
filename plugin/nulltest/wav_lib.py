"""WAV read/write (16/24/32-bit int and 32-bit float) with numpy."""
import struct, numpy as np

def write_float(path, data, sr):
    data = np.asarray(data, dtype=np.float32)
    if data.ndim == 1: data = data[:, None]
    ch = data.shape[1]
    raw = data.tobytes()
    fmt = struct.pack('<HHIIHH', 3, ch, sr, sr * ch * 4, ch * 4, 32)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(raw)) + raw
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)

def read(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'RIFF' and b[8:12] == b'WAVE'
    i = 12; fmt = None; data = None
    while i + 8 <= len(b):
        cid = b[i:i+4]; n = struct.unpack('<I', b[i+4:i+8])[0]; body = b[i+8:i+8+n]
        if cid == b'fmt ': fmt = struct.unpack('<HHIIHH', body[:16]) + ((struct.unpack('<H', body[24:26])[0],) if len(body) >= 26 else (None,))
        elif cid == b'data': data = body
        i += 8 + n + (n & 1)
    tag, ch, sr, _, _, bits, sub = fmt
    if tag == 0xFFFE: tag = sub
    if tag == 3: x = np.frombuffer(data, '<f4' if bits == 32 else '<f8').astype(np.float64)
    elif bits == 16: x = np.frombuffer(data, '<i2') / 32768.0
    elif bits == 24:
        a = np.frombuffer(data, np.uint8).reshape(-1, 3).astype(np.int32)
        v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16); v[v >= 1 << 23] -= 1 << 24
        x = v / 8388608.0
    elif bits == 32: x = np.frombuffer(data, '<i4') / 2147483648.0
    return x.reshape(-1, ch), sr
