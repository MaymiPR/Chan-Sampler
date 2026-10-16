import flp, struct, sys
def make(out, sets):
    h, ev = flp.read('probe/p1.flp'); ch = -1
    for e in ev:
        if e[0] == 64: ch = flp.val(e[1])
        if e[0] == 215 and ch == 0:
            for o, fmt, v in sets: struct.pack_into(fmt, e[1], o, v)
    flp.write(out, h, ev)
if __name__ == '__main__':
    import ast
    make(sys.argv[1], ast.literal_eval(sys.argv[2]))
