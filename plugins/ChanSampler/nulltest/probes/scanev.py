"""Write a test project with channel events changed (python -m probes.scanev out.flp "[(event, fmt, value)]")."""
import struct, sys, ast; from lib import flp
h, ev = flp.read('probe/p1.flp'); ch = -1
sets = ast.literal_eval(sys.argv[2])   # [(eid, fmt, value)]
for e in ev:
    if e[0] == 64: ch = flp.val(e[1])
    for eid, fmt, v in sets:
        if e[0] == eid and ch == 0: e[1] = bytearray(struct.pack(fmt, v))
flp.write(sys.argv[1], h, ev)
