"""Write a test project with channel events changed (python -m probe_events out.flp "[(event, fmt, value)]")."""
import struct, sys, ast; import project_file_lib
h, ev = project_file_lib.read('probe/p1.flp'); ch = -1
sets = ast.literal_eval(sys.argv[2])   # [(eid, fmt, value)]
for e in ev:
    if e[0] == 64: ch = project_file_lib.val(e[1])
    for eid, fmt, v in sets:
        if e[0] == eid and ch == 0: e[1] = bytearray(struct.pack(fmt, v))
project_file_lib.write(sys.argv[1], h, ev)
