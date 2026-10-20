"""Segment-aware disassembler for CIV.EXE (root + 23 overlays) built on objdump.

    python3 -I tools/disasm.py ~/Dos/Civ1/CIV.EXE build/asm

Writes one listing per root segment (seg_XXXX.asm) and per overlay (ovl_NN.asm), plus
symbols.txt. Far calls become `call f_SSSS_OOOO`, overlay thunks (`int 3Fh; db n; dw off`)
become `call o_NN_OOOO`, and near calls inside a segment are named the same way.
Names from tools/symbols.map (`f_SSSS_OOOO name` per line) replace the generated ones.
"""
import os, re, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(__file__))
from unexepack import unpack

DGROUP = 0x2A1B
OVL_BASE = 0x2388          # link-time segment every overlay is loaded at
OVL_MARK = 0xF000          # fake segment used to tag rewritten overlay thunks


def load(path):
    exe, _ = unpack(open(path, 'rb').read())
    nrel, hdrp = struct.unpack('<HH', exe[6:10])
    relo, = struct.unpack('<H', exe[24:26])
    last, pages = struct.unpack('<HH', exe[2:6])
    img_end = (pages - 1) * 512 + (last or 512)
    root = bytearray(exe[hdrp * 16:img_end])
    segs = set()
    for i in range(nrel):
        off, seg = struct.unpack('<HH', exe[relo + 4 * i:relo + 4 * i + 4])
        a = seg * 16 + off
        segs.add(struct.unpack('<H', root[a:a + 2])[0])
    overlays = {}
    raw = open(path, 'rb').read()
    p = raw.find(b'MZ', 0x2b000)
    while p >= 0:
        last, pages, nrel, hdr = struct.unpack('<4H', raw[p + 2:p + 10])
        num, = struct.unpack('<H', raw[p + 26:p + 28])
        size = (pages - 1) * 512 + (last or 512)
        overlays[num] = bytearray(raw[p + hdr * 16:p + size])
        p = raw.find(b'MZ', p + size)
    return root, sorted(s for s in segs if s < DGROUP), overlays


def rewrite_thunks(code):
    """Turn `int 3Fh; db n; dw off` into `call far F0nn:off` so objdump stays in sync."""
    i = 0
    while (i := code.find(b'\xcd\x3f', i)) >= 0:
        n = code[i + 2]
        if 1 <= n <= 23:
            code[i:i + 5] = b'\x9a' + code[i + 3:i + 5] + struct.pack('<H', OVL_MARK + n)
            i += 5
        else:
            i += 2
    return code


def jump_tables(code):
    """MSC switch dispatch: cmp ax,N / jbe / jmp default / add ax,ax / xchg bx,ax / jmp cs:[bx+T].
    Returns {table offset: [targets]}."""
    tables = {}
    i = 0
    while (i := code.find(b'\x2e\xff\xa7', i)) >= 0:
        t = struct.unpack('<H', code[i + 3:i + 5])[0]
        n = None
        for back in range(4, 24):
            j = i - back
            if j < 0:
                break
            if code[j] == 0x3d:
                n = struct.unpack('<H', code[j + 1:j + 3])[0]
                break
            if code[j] == 0x83 and code[j + 1] == 0xf8:
                n = code[j + 2]
                break
        if n is not None and t + 2 * (n + 1) <= len(code):
            tables[t] = [struct.unpack('<H', code[t + 2 * k:t + 2 * k + 2])[0] for k in range(n + 1)]
        i += 5
    return tables


def objdump(code):
    tables = jump_tables(code)
    ranges, start = [], 0
    for t in sorted(tables):
        ranges.append((start, t))
        start = t + 2 * len(tables[t])
    ranges.append((start, len(code)))
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(code)
    lines = []
    try:
        for a, b in ranges:
            if a >= b:
                continue
            out = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '-Maddr16,data16,intel',
                                  '--start-address=%d' % a, '--stop-address=%d' % b, f.name],
                                 capture_output=True, text=True, check=True).stdout
            for ln in out.splitlines():
                m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t?(.*)', ln)
                if m:
                    lines.append((int(m.group(1), 16), m.group(2).strip(), m.group(3).strip()))
            if b in tables:
                for k, tgt in enumerate(tables[b]):
                    lines.append((b + 2 * k, '%02x %02x' % (tgt & 0xFF, tgt >> 8), '.case %d L%04x' % (k, tgt)))
    finally:
        os.unlink(f.name)
    return lines


def far_name(seg, off):
    if seg >= OVL_MARK:
        return 'o_%02d_%04x' % (seg - OVL_MARK, off)
    return 'f_%04x_%04x' % (seg, off)


def main(exe, outdir):
    root, segs, overlays = load(exe)
    os.makedirs(outdir, exist_ok=True)
    names = {}
    globs = {}
    mp = os.path.join(os.path.dirname(__file__), 'symbols.map')
    if os.path.exists(mp):
        for ln in open(mp):
            parts = ln.split('#')[0].split()
            if len(parts) >= 2:
                if parts[0].startswith('g_'):
                    globs[int(parts[0][2:], 16)] = parts[1]
                else:
                    names[parts[0]] = parts[1]

    def globname(m):
        a = int(m.group(2), 16)
        if m.group(1) == '-':
            a = 0x10000 - a
        if a in globs:
            return '%s%s' % ('ds:' if m.group(1) == 'ds:' else '+', globs[a])
        return m.group(0)
    gref = re.compile(r'(ds:|[+-])0x([0-9a-f]{3,4})\b')

    units = []
    bounds = segs + [DGROUP]
    for a, b in zip(bounds, bounds[1:]):
        if OVL_BASE <= a < 0x25e3:
            continue
        end = b * 16 + 64 if b != DGROUP else b * 16   # code may run a little past the next segment base
        units.append(('seg_%04x' % a, a, rewrite_thunks(bytearray(root[a * 16:end]))))
    for n, code in sorted(overlays.items()):
        units.append(('ovl_%02d' % n, OVL_MARK + n, rewrite_thunks(code)))

    far = re.compile(r'(call|jmp)\s+0x([0-9a-f]+):0x([0-9a-f]+)')
    near = re.compile(r'(call|jmp|j\w+|loop\w*)\s+0x([0-9a-f]+)$')
    casere = re.compile(r'\.case \d+ L([0-9a-f]{4})$')
    called = {}
    listings = {}
    for uname, useg, code in units:
        lines = objdump(code)
        listings[uname] = (useg, lines)
        for off, hexb, ins in lines:
            m = far.search(ins)
            if m:
                s, o = int(m.group(2), 16), int(m.group(3), 16)
                if useg >= OVL_MARK and s == OVL_BASE:
                    s = useg
                called.setdefault(far_name(s, o), set()).add(uname)
            m = near.search(ins)
            if m and ins.startswith('call'):
                called.setdefault(far_name(useg, int(m.group(2), 16)), set()).add(uname)

    def sym(s):
        return names.get(s, s)

    # Printable NUL-terminated strings in DGROUP's initialised data, for comments.
    ds = root[DGROUP * 16:]
    def string_at(a):
        if a < 0x40 or a >= len(ds):
            return None
        e = ds.find(b'\0', a, a + 200)
        if e - a < 3 or any(c < 32 or c > 126 for c in ds[a:e]):
            return None
        return ds[a:e].decode('latin1')
    imm = re.compile(r'^(mov\s+\w\w,|push\s+)0x([0-9a-f]{3,4})$')

    # Functions reached only through pointers still start with a standard prologue.
    for uname, (useg, lines) in listings.items():
        for i in range(len(lines) - 1):
            if lines[i][1] == '55' and lines[i + 1][1] == '8b ec':
                called.setdefault(far_name(useg, lines[i][0]), set())
    index = []

    for uname, (useg, lines) in listings.items():
        with open(os.path.join(outdir, uname + '.asm'), 'w') as f:
            for off, hexb, ins in lines:
                key = far_name(useg, off)
                if key in called:
                    f.write('\n%s:%s\n' % (sym(key), '' if sym(key) == key else '  ; ' + key))
                    index.append([sym(key), uname, 0, [], set()])
                if index:
                    index[-1][2] += 1
                m = far.search(ins)
                if m:
                    s, o = int(m.group(2), 16), int(m.group(3), 16)
                    if useg >= OVL_MARK and s == OVL_BASE:
                        s = useg
                    ins = ins[:m.start()] + '%s %s' % (m.group(1), sym(far_name(s, o))) + ins[m.end():]
                elif (m := near.search(ins)):
                    tgt = int(m.group(2), 16)
                    if ins.startswith('call'):
                        ins = 'call %s' % sym(far_name(useg, tgt))
                    else:
                        ins = '%s L%04x' % (m.group(1), tgt)
                if globs:
                    ins = gref.sub(globname, ins)
                m = imm.match(ins)
                if m and (txt := string_at(int(m.group(2), 16))):
                    ins += '    ; "%s"' % txt
                    if index:
                        index[-1][3].append(txt)
                if index and ins.startswith('call'):
                    index[-1][4].add(ins.split()[1])
                f.write('  %04x  %-20s %s\n' % (off, hexb, ins))
    with open(os.path.join(outdir, 'symbols.txt'), 'w') as f:
        for k in sorted(called):
            f.write('%s %s  callers=%s\n' % (k, sym(k), ','.join(sorted(called[k]))))
    with open(os.path.join(outdir, 'functions.txt'), 'w') as f:
        for name, uname, n, strs, callees in index:
            f.write('%-28s %-9s %5d  calls=%s\n' % (name, uname, n, ','.join(sorted(callees))))
            for t in strs[:12]:
                f.write('      "%s"\n' % t)
    print('units', len(units), 'functions', len(called))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
