"""Name the SaveData field (or known global) at DGROUP offsets: dsfield.py 0xf5b8 0x6c2c ..."""
import re, subprocess, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sve_layout import SVE_LAYOUT

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fields():
    src = open(os.path.join(ROOT, 'src/game.h')).read()
    body = src[src.index('typedef struct {\n    int16_t  turn;'):]
    body = body[:body.index('} SaveData;')]
    out = []
    for line in body.splitlines()[1:]:
        m = re.match(r'\s+\w+\s+(\w+)', line)
        if m:
            out.append(m.group(1))
    return out


def offsets():
    """(offset, size, name) of each SaveData field, from the compiler."""
    names = fields()
    src = os.path.join(ROOT, 'build', 'offsets.c')
    exe = os.path.join(ROOT, 'build', 'offsets')
    with open(src, 'w') as f:
        f.write('#include <stdio.h>\n#include <stddef.h>\n#include "game.h"\nint main(void) {\n')
        for n in names:
            f.write('    printf("%%zu %%zu %s\\n", offsetof(SaveData, %s), sizeof(((SaveData *)0)->%s));\n' % (n, n, n))
        f.write('    return 0;\n}\n')
    cc = os.path.join(ROOT, '.toolchain', 'gcc')
    subprocess.check_call([cc if os.path.exists(cc) else 'gcc', '-I', os.path.join(ROOT, 'src'), src, '-o', exe])
    out = subprocess.check_output([exe]).decode().split('\n')
    return [(int(a), int(b), c) for a, b, c in (l.split() for l in out if l)]


def lookup(addr, _cache=[]):
    """Blocks of the save hold one field, or several whose sizes add up to the block's (the
    struct may store a record more compactly than the game, e.g. unit_types)."""
    if not _cache:
        _cache.append(offsets())
    flds = list(_cache[0])
    k = 0
    for seg, a, size in SVE_LAYOUT:
        group, total = [flds[k]], flds[k][1]
        k += 1
        if group[0][2] == 'unit_types':             # the game's 34-byte records hold the names too
            group.append(flds[k])
            k += 1
            if seg == 'ds' and int(a, 16) <= addr < int(a, 16) + size:
                return 'unit_types[%d] byte %d' % divmod(addr - int(a, 16), 34)
            continue
        while total < size and k < len(flds) and total + flds[k][1] <= size:
            group.append(flds[k])
            total += flds[k][1]
            k += 1
        if seg == 'ds' and a.startswith('0x') and int(a, 16) <= addr < int(a, 16) + size:
            off = addr - int(a, 16)
            for _, n, name in group:
                if off < n or len(group) == 1:
                    return '%s+%d' % (name, off) if off else name
                off -= n
    return None


if __name__ == '__main__':
    for v in sys.argv[1:]:
        print(v, lookup(int(v, 16)))
