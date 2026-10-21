"""Put a Chan Sampler (VST3 via FL's Fruity Wrapper) channel into a test project.

The wrapper's two channel events (212, 213) come from an FL project that held
Chan Sampler, saved in data/vst_wrapper.bin. Event 213's record 3 holds the
plugin's VST3 state, which is JUCE's "VC2!" XML chunk plus a small private
block; we replace the XML and fix the two size fields: the record's own int64
size and the int64 "remaining bytes" field at offset 334.
"""
import os, struct, xml.sax.saxutils as sx
from lib import flp, paths

WRAPPER = os.path.join(paths.DATA, 'vst_wrapper.bin')   # <u32 len212><u32 len213><event 212><event 213>

def _wrapper_events():
    d = open(WRAPPER, 'rb').read()
    n212, n213 = struct.unpack_from('<II', d)
    return d[8:8 + n212], d[8 + n212:8 + n212 + n213]

def state_xml(sample_path, params):
    attrs = "".join(f'<PARAM id="{k}" value="{float(v)!r}"/>' for k, v in params.items())
    return f'<?xml version="1.0" encoding="UTF-8"?>\n\n<STATE samplePath={sx.quoteattr(sample_path)}>{attrs}</STATE>\n'

def plugin_data(sample_path, params):
    _, d = _wrapper_events()
    vc = d.find(b"VC2!")
    rec_size_off = vc - 8                      # int64 size of record 3
    old_rec_size = struct.unpack_from('<Q', d, rec_size_off)[0]
    old_xml_len = struct.unpack_from('<I', d, vc + 4)[0]
    old_vc2_len = 8 + old_xml_len
    x = state_xml(sample_path, params).encode('utf-8') + b'\0'
    new_vc2 = b"VC2!" + struct.pack('<I', len(x)) + x
    delta = len(new_vc2) - old_vc2_len
    out = bytearray(d[:vc] + new_vc2 + d[vc + old_vc2_len:])
    struct.pack_into('<Q', out, rec_size_off, old_rec_size + delta)
    total = struct.unpack_from('<Q', out, 334)[0]
    struct.pack_into('<Q', out, 334, total + delta)
    return bytes(out)

def make_vst_channel(template_channel_events, sample_path, params):
    """Turn a copy of a Sampler channel's events into a Chan Sampler channel."""
    p212, _ = _wrapper_events()
    data = plugin_data(sample_path, params)
    out = []
    for eid, v in template_channel_events:
        if eid == 196:          # sample path belongs to the Sampler only
            continue
        if eid == 21:
            v = bytearray([4])  # instrument / plugin channel
        elif eid == 201:
            v = bytearray("Fruity Wrapper\0".encode('utf-16le'))
        elif eid == 212:
            v = bytearray(p212)
        elif eid == 203:
            v = bytearray("Chan Sampler\0".encode('utf-16le'))
        out.append([eid, bytearray(v)])
        if eid == 41:
            out.append([213, bytearray(data)])
    return out

def build_vst_project(out_path, sample_path, params, notes, sampler_patch=None):
    """Basic template with channel 1 replaced by Chan Sampler; notes = (start, len, key, vel, channel)."""
    from lib import build_project as B
    header, ev = flp.read(B.TEMPLATE)
    # channel 0: Sampler playing the same sample (optionally patched)
    B.point_sampler(ev, sample_path)
    # Chan Sampler's channel is built from the unpatched Sampler channel: FL's
    # channel levels (volume / pan) apply to plugin channels too.
    starts = [i for i, e in enumerate(ev) if e[0] == 64]
    c0 = [[eid, bytearray(v)] for eid, v in ev[starts[0] + 1:starts[1]]]
    if sampler_patch:
        sampler_patch(ev)
    starts = [i for i, e in enumerate(ev) if e[0] == 64]
    c1s, c1e = starts[1], starts[2]
    ev[c1s + 1:c1e] = make_vst_channel(c0, sample_path, params)
    B.add_pattern(ev, notes)
    flp.write(out_path, header, ev)
