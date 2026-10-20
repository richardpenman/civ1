"""Lift one function of a build/asm listing into goto-style pseudo-C.

    python3 -I tools/lift.py build/asm/seg_0d07.asm f_0d07_0043 > out.c

The original is Microsoft C 5.1 output with almost no optimisation, so a per-basic-block
symbolic register tracker recovers most expressions.  The result is a reading aid for
writing the real C port, not compilable code.  Locals are lN (= [bp-N]), arguments aN
(= [bp+N], first argument is a6 for far functions).  City / unit / civ-array accesses are
folded into city[i].field, unit[c][u].field and name[i] using tools/symbols.map.
"""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))

CITY_FIELDS = {0: 'buildings', 2: 'buildings_hi', 4: 'x', 5: 'y', 6: 'status', 7: 'size',
               8: 'visible_size', 9: 'producing', 10: 'trade', 11: 'owner', 12: 'food',
               14: 'shields', 16: 'worked', 18: 'worked_hi', 20: 'unk20', 22: 'name',
               23: 'trade_routes', 24: 'trade_routes+1', 25: 'trade_routes+2', 26: 'unk26',
               27: 'unk26+1'}
UNIT_FIELDS = {0: 'status', 1: 'x', 2: 'y', 3: 'type', 4: 'moves', 5: 'special', 6: 'goto_x',
               7: 'goto_y', 8: 'unk8', 9: 'visible', 10: 'next', 11: 'home_city'}
UTYPE_FIELDS = {12: 'obsolete_by', 14: 'domain', 16: 'moves', 18: 'fuel', 20: 'attack',
                22: 'defense', 24: 'cost', 26: 'sight', 28: 'capacity', 30: 'role', 32: 'prereq'}
CITIES, UNITS, UTYPES = 0x706c, 0x90f4, 0x112a

JCC = {'je': '==', 'jne': '!=', 'jl': '<', 'jle': '<=', 'jg': '>', 'jge': '>=',
       'jb': '<u', 'jbe': '<=u', 'ja': '>u', 'jae': '>=u', 'js': '<0', 'jns': '>=0'}
REG16 = {'al': 'ax', 'ah': 'ax', 'bl': 'bx', 'bh': 'bx', 'cl': 'cx', 'ch': 'cx', 'dl': 'dx', 'dh': 'dx'}


def load_symbols():
    funcs, globs = {}, {}
    global FAR
    FAR = {}
    p = os.path.join(HERE, 'symbols.map')
    if os.path.exists(p):
        for ln in open(p):
            parts = ln.split('#')[0].split()
            if len(parts) >= 2:
                if parts[0].startswith('g_'):
                    globs[int(parts[0][2:], 16)] = parts[1]
                elif parts[0].startswith('h_'):
                    seg, off = parts[0][2:].split('_')
                    FAR[(int(seg, 16), int(off, 16))] = parts[1]
                else:
                    funcs[parts[0]] = parts[1]
    return funcs, globs


FUNCS, GLOBS = load_symbols()
_ROOT_BIN = os.path.join(os.path.dirname(HERE), 'build/root.bin')
DSIMG = open(_ROOT_BIN, 'rb').read()[0x2a1b0:] if os.path.exists(_ROOT_BIN) else b''


def far_name(seg, off):
    best = None
    for (s_, o_), name in FAR.items():
        if s_ == seg and o_ <= off and (best is None or o_ > best[0]):
            best = (o_, name)
    if best and off - best[0] < 0x1000:
        return best[1] if off == best[0] else '%s+%d' % (best[1], off - best[0])
    return 'far_%04x_%04x' % (seg, off)
NAME_TO_ADDR = {v: k for k, v in GLOBS.items()}
GLOB_SORTED = sorted(GLOBS)


def glob_name(a):
    """Name for DGROUP address a: exact symbol, or symbol+offset within 64 bytes."""
    if a in GLOBS:
        return GLOBS[a]
    import bisect
    i = bisect.bisect_right(GLOB_SORTED, a) - 1
    return 'g_%04x' % a


def paren(e):
    return e if re.fullmatch(r'[\w.\[\]]+', e) else '(%s)' % e


def stride_index(expr, stride):
    """If expr is 'i * stride' (as built by imul), return i."""
    m = re.fullmatch(r'\((.+) \* %d\)' % stride, expr) or re.fullmatch(r'(.+) \* %d' % stride, expr)
    return m.group(1) if m else None


def fold_mem(base_regs, disp, size, regs):
    """Render [regs + disp] using known array layouts."""
    parts = [regs.get(r, r) for r in base_regs]
    if disp is not None and disp >= 0x8000:
        disp -= 0x10000
    a = disp & 0xFFFF if disp is not None else None
    # cities[i]
    for i, p in enumerate(parts):
        idx = stride_index(p, 28)
        if idx is not None and a is not None and CITIES <= a < CITIES + 28 and len(parts) == 1:
            return 'city[%s].%s' % (idx, CITY_FIELDS.get(a - CITIES, '+%d' % (a - CITIES)))
    # units[c][u]: (c*1536) + (u*12)
    if a is not None and UNITS <= a < UNITS + 12:
        civ = unit = None
        for p in parts:
            m = re.fullmatch(r'\((.+) \* 1536\) \+ \((.+) \* 12\)', p)
            if m:
                civ, unit = m.group(1), m.group(2)
            elif stride_index(p, 1536) is not None:
                civ = stride_index(p, 1536)
            elif stride_index(p, 12) is not None:
                unit = stride_index(p, 12)
        if civ is not None and unit is not None:
            return 'unit[%s][%s].%s' % (civ, unit, UNIT_FIELDS[a - UNITS])
    if a is not None and UTYPES <= a < UTYPES + 28 * 34 and len(parts) == 1:
        idx = stride_index(parts[0], 34)
        if idx is not None:
            off = a - UTYPES
            return 'utype[%s].%s' % (idx, UTYPE_FIELDS.get(off, '+%d' % off))
    if a is not None and len(parts) == 1:
        p = parts[0]
        m = re.fullmatch(r'\(?(.+?) \* 2\)?', p)
        if m:
            return '%s[%s]' % (glob_name(a), m.group(1))
        return '%s[%s]' % (glob_name(a), p)
    if a is not None and not parts:
        return glob_name(a)
    s = ' + '.join(parts)
    if a:
        s += ' + %s' % glob_name(a)
    return '%s[%s]' % ('BYTE' if size == 1 else 'WORD', s)


def idioms(v):
    v = re.sub(r'^-\(-\((.+) <u 1\)\)$', r'!\1', v)
    v = re.sub(r'^-\(-\((.+) <u (.+)\)\)$', r'(\1 <u \2)', v)
    for _ in range(3):
        v = re.sub(r'\(\(\(abs\((.+?)\)\) >> (\d+)\) \^ \(SIGN\(\1\)\)\) - \(SIGN\(\1\)\)',
                   lambda m: '(%s) / %d' % (m.group(1), 1 << int(m.group(2))), v)
        v = re.sub(r'\(\((.+?)\) - \(SIGN\(\1\)\)\) >> 1', r'(\1) / 2', v)
        v = re.sub(r'\((.+?) - \(SIGN\(\1\)\)\) >> 1', r'\1 / 2', v)
        v = re.sub(r'\(\((.+?)\) \^ \(SIGN\(\1\)\)\) - \(SIGN\(\1\)\)', r'abs(\1)', v)
        v = re.sub(r'\((.+?) \^ \(SIGN\(\1\)\)\) - \(SIGN\(\1\)\)', r'abs(\1)', v)
    return v


class Lifter:
    def __init__(self, lines):
        self.lines = lines
        self.out = []
        self.regs = {}
        self.stack = []
        self.cond = None
        self.tables = {}
        cur = None
        for off, ins in lines:
            if ins.startswith('.case'):
                k, lab = ins.split()[1:]
                if k == '0':
                    cur = off
                self.tables.setdefault(cur, []).append(lab)
        targets = set()
        self.npred = {}
        for _, ins in lines:
            for m in re.finditer(r'\bL([0-9a-f]{4})\b', ins):
                t = int(m.group(1), 16)
                targets.add(t)
                self.npred[t] = self.npred.get(t, 0) + (99 if ins.startswith('.case') else 1)
        self.snap = {}
        self.targets = targets

    def operand(self, op):
        op = op.strip()
        size = 1 if op.startswith('BYTE') else 2
        m = re.fullmatch(r'(?:BYTE|WORD|DWORD) PTR (?:(\w\w):)?\[(.+)\]', op)
        if m and m.group(1) == 'es' and str(self.regs.get('es', '')).startswith('SEG:'):
            seg = int(self.regs['es'][4:], 16)
            regs, disp = [], 0
            for t in re.findall(r'[+-]?[^+-]+', m.group(2)):
                t = t.strip()
                if re.fullmatch(r'[+-]?0x[0-9a-f]+', t):
                    disp = int(t.replace('+', ''), 16) if not t.startswith('-') else -int(t[1:], 16)
                else:
                    regs.append(self.regs.get(t.lstrip('+'), t.lstrip('+')))
            idx = ' + '.join(regs) or '0'
            return '%s[%s]' % (far_name(seg, disp & 0xFFFF), idx)
        if m:
            inner = m.group(2)
            if re.fullmatch(r'bp([+-])0x([0-9a-f]+)', inner):
                s, n = re.fullmatch(r'bp([+-])0x([0-9a-f]+)', inner).groups()
                return ('l%d' if s == '-' else 'a%d') % int(n, 16)
            m2 = re.fullmatch(r'bp\+(\w\w)([+-]0x[0-9a-f]+)', inner)
            if m2:
                return 'local_array[%s %s]' % (self.regs.get(m2.group(1), m2.group(1)), m2.group(2))
            regs, disp = [], None
            for t in re.findall(r'[+-]?[^+-]+', inner):
                t = t.strip()
                if re.fullmatch(r'[+-]?0x[0-9a-f]+', t):
                    disp = int(t.replace('+', ''), 16) & 0xFFFF if not t.startswith('-') else (-int(t[1:], 16)) & 0xFFFF
                elif t.lstrip('+-') in NAME_TO_ADDR:
                    disp = NAME_TO_ADDR[t.lstrip('+-')]
                else:
                    regs.append(t.lstrip('+'))
            return fold_mem(regs, disp, size, self.regs)
        m = re.fullmatch(r'(?:BYTE|WORD|DWORD) PTR (?:ds:)?([\w:]+)', op)
        if m:
            t = m.group(1)
            if t.startswith('ds:'):
                t = t[3:]
            if t.startswith('0x'):
                return glob_name(int(t, 16))
            return t
        if op.startswith('ds:'):
            t = op[3:]
            return glob_name(int(t, 16)) if t.startswith('0x') else t
        if re.fullmatch(r'0x[0-9a-f]+', op):
            v = int(op, 16)
            return str(v - 0x10000 if v >= 0x8000 else v) if v > 9 else str(v)
        if op in REG16:
            return self.regs.get(REG16[op], REG16[op])
        if op in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'es', 'ds'):
            return self.regs.get(op, op)
        return op

    def emit(self, s):
        self.out.append('    ' + s)

    def invalidate(self, mem):
        """Memory `mem` is about to change: registers whose value reads it get a temporary."""
        for r, v in list(self.regs.items()):
            if isinstance(v, str) and mem in v and v != mem + '_hi':
                self.tmp = getattr(self, 'tmp', 0) + 1
                name = 't%d' % self.tmp
                self.emit('%s = %s;' % (name, v))
                self.regs[r] = name

    def flush_regs(self):
        self.regs = {}
        self.dirty = set()

    def spill(self):
        """Make a register value that crosses a block boundary visible (MSC ternaries)."""
        for r in ('ax', 'dx', 'cx'):
            if r in getattr(self, 'dirty', ()) and not self.regs.get(r, '').startswith('r_'):
                self.emit('%s = %s;' % (r, self.regs[r]))
        self.dirty = set()

    def setreg(self, r, v):
        r = REG16.get(r, r)
        v = idioms(v)
        if not hasattr(self, 'dirty'):
            self.dirty = set()
        self.dirty.add(r)
        self.regs[r] = v

    def is_mem(self, op):
        return 'PTR' in op or op.startswith('ds:') or op.startswith('es:')

    def run(self):
        L = self.lines
        i = 0
        while i < len(L):
            off, ins = L[i]
            if off in self.targets:
                prev = L[i - 1][1].split()[0] if i else 'jmp'
                falls = prev not in ('jmp', 'ret', 'retf', 'iret')
                n = self.npred.get(off, 0) + (1 if falls else 0)
                self.spill()
                self.out.append('L%04x:' % off)
                if n == 1 and not falls and off in self.snap:
                    self.regs = dict(self.snap[off])
                    self.dirty = set()
                elif not (n == 1 and falls):
                    self.flush_regs()
                    self.stack = []
            parts = ins.split(None, 1)
            op = parts[0]
            if op == '.case':
                args = [parts[1]]
            else:
                args = [a.strip() for a in re.split(r',(?![^\[]*\])', parts[1].split(';')[0])] if len(parts) > 1 else []
            comment = ins.split(';', 1)[1].strip() if ';' in ins else ''
            self.step(op, args, comment, i)
            i += 1
        return '\n'.join(self.out)

    def step(self, op, args, comment, i):
        R = self.operand
        if op in ('push',) and args[0] in ('bp', 'si', 'di') and i < 6:
            return
        if op == 'pop' and args[0] in ('bp', 'si', 'di'):
            return
        if op == 'mov' and args == ['bp', 'sp'] or op == 'mov' and args == ['sp', 'bp']:
            return
        if op == 'sub' and args[0] == 'sp' and i < 6:
            return
        if op == 'push':
            if args[0] == 'cs':
                return
            v = R(args[0])
            if comment:
                v = comment
            self.stack.append(v)
            return
        if op == 'pop':
            if self.stack:
                self.setreg(args[0], self.stack.pop())
            return
        if op == 'mov' and args[0] == 'es':
            mm = re.fullmatch(r'WORD PTR ds:0x([0-9a-f]+)', args[1])
            if mm and DSIMG:
                a = int(mm.group(1), 16)
                self.regs['es'] = 'SEG:%04x' % struct.unpack('<H', DSIMG[a:a + 2])[0]
            else:
                self.regs['es'] = self.operand(args[1])
            return
        if op == 'mov':
            d, s = args
            v = R(s)
            if comment and re.fullmatch(r'0x[0-9a-f]+', s.strip()):
                v = comment
            if self.is_mem(d):
                self.invalidate(R(d))
                self.emit('%s = %s;' % (R(d), v))
            else:
                if s.strip() in REG16 or d in REG16 and d.endswith('h'):
                    pass
                self.setreg(d, v)
            return
        if op == 'lea':
            self.setreg(args[0], '&' + R(args[1]))
            return
        if op in ('cbw',):
            self.setreg('ax', '(signed char)' + paren(self.regs.get('ax', 'ax')))
            return
        if op == 'cwd':
            self.setreg('dx', 'SIGN(%s)' % self.regs.get('ax', 'ax'))
            return
        if op == 'xchg' and sorted(args) == ['ax', 'bx'] and i + 1 < len(self.lines) and 'cs:[bx+' in self.lines[i + 1][1]:
            v = self.regs.get('ax', 'ax')
            m = re.fullmatch(r'\((.+)\) \+ \(\1\)|(.+) \+ \2', v)
            self.regs['ax'] = (m.group(1) or m.group(2)) if m else '(%s) / 2' % v
            return
        if op in ('xchg',):
            a, b = args
            va, vb = R(a), R(b)
            if self.is_mem(a):
                self.emit('%s = %s;' % (va, vb))
            else:
                self.setreg(a, vb)
            if self.is_mem(b):
                self.emit('%s = %s;' % (vb, va))
            else:
                self.setreg(b, va)
            return
        if op == 'sbb' and len(args) == 2 and args[0] == args[1] and self.cond:
            a, b = self.cond
            self.setreg(args[0], '-(%s <u %s)' % (a, b))
            return
        binops = {'add': '+', 'sub': '-', 'and': '&', 'or': '|', 'xor': '^', 'shl': '<<', 'sar': '>>',
                  'shr': '>>u', 'adc': '+c', 'sbb': '-c', 'rcl': 'rcl', 'imul': '*'}
        if op in binops and len(args) == 2:
            d, s = args
            if op in ('sub', 'xor') and d == s:
                if op == 'sub' and d in ('ah', 'ch', 'bh', 'dh'):
                    r = REG16[d]
                    self.setreg(r, '(unsigned char)' + paren(self.regs.get(r, r)))
                    return
                self.setreg(d, '0')
                self.cond = (R(d), '0')
                return
            if op == 'or' and d == s:
                self.cond = (R(d), '0')
                return
            vs = R(s)
            if s == '1' and op in ('shl', 'sar', 'shr'):
                vs = '1'
            if self.is_mem(d):
                self.invalidate(R(d))
                self.emit('%s %s= %s;' % (R(d), binops[op], vs))
                self.cond = (R(d), '0')
            else:
                expr = '%s %s %s' % (paren(R(d)), binops[op], paren(vs))
                if op == 'shl' and vs == '1' and stride_index(paren(R(d)), 1) is None:
                    expr = '%s * 2' % paren(R(d))
                    m = re.fullmatch(r'\((.+) \* (\d+)\) \* 2', expr)
                    if m:
                        expr = '%s * %d' % (m.group(1), int(m.group(2)) * 2)
                self.setreg(d, expr)
                self.cond = (expr, '0')
            return
        if op == 'imul' and len(args) == 1:
            a = self.regs.get('ax', 'ax')
            self.setreg('ax', '%s * %s' % (paren(R(args[0])), paren(a)) if a.isdigit() else '%s * %s' % (paren(a), paren(R(args[0]))))
            # normalise "N * x" -> "x * N"
            m = re.fullmatch(r'(\d+) \* (.+)', self.regs['ax'])
            if m:
                self.regs['ax'] = '%s * %s' % (m.group(2), m.group(1))
            return
        if op in ('mul',) and len(args) == 1:
            self.setreg('ax', '%s * %s' % (paren(self.regs.get('ax', 'ax')), paren(R(args[0]))))
            return
        if op in ('idiv', 'div') and len(args) == 1:
            a = self.regs.get('ax', 'ax')
            d = R(args[0])
            self.setreg('ax', '%s / %s' % (paren(a), paren(d)))
            self.setreg('dx', '%s %% %s' % (paren(a), paren(d)))
            return
        if op in ('neg', 'not'):
            d = args[0]
            sym = '-' if op == 'neg' else '~'
            if self.is_mem(d):
                self.emit('%s = %s%s;' % (R(d), sym, paren(R(d))))
            else:
                self.setreg(d, sym + paren(R(d)))
            return
        if op in ('inc', 'dec'):
            d = args[0]
            sym = '++' if op == 'inc' else '--'
            if self.is_mem(d):
                self.invalidate(R(d))
                self.emit('%s%s;' % (R(d), sym))
                self.cond = (R(d), '0')
            else:
                self.setreg(d, '%s %s 1' % (paren(R(d)), '+' if op == 'inc' else '-'))
                self.cond = (self.regs[REG16.get(d, d)], '0')
            return
        if op == 'cmp':
            self.cond = (R(args[0]), R(args[1]))
            return
        if op == 'test':
            a, b = R(args[0]), R(args[1])
            self.cond = (a if a == b else '(%s & %s)' % (paren(a), paren(b)), '0')
            return
        if op in JCC:
            m = re.match(r'L([0-9a-f]+)', args[0])
            a, b = self.cond or ('?', '?')
            rel = JCC[op]
            if rel in ('<0', '>=0'):
                c = '%s %s 0' % (a, rel[0:-1])
            else:
                c = '%s %s %s' % (a, rel, b)
            self.emit('if (%s) goto L%s;' % (c, m.group(1)))
            self.snap[int(m.group(1), 16)] = dict(self.regs)
            return
        if op == '.case':
            return
        if op == 'jmp' and args and args[0].startswith('WORD PTR cs:'):
            t = int(re.search(r'0x([0-9a-f]+)\]', args[0]).group(1), 16)
            self.emit('switch (%s) {' % self.regs.get('ax', 'ax'))
            labs = self.tables.get(t, [])
            groups = {}
            for k, lab in enumerate(labs):
                groups.setdefault(lab, []).append(k)
            for lab, ks in groups.items():
                self.emit('    %s goto %s;' % (' '.join('case %d:' % k for k in ks) if len(ks) < 6 else
                                               'default/* %d cases */:' % len(ks), lab))
            self.emit('}')
            return
        if op == 'jmp':
            m = re.match(r'L([0-9a-f]+)', args[0])
            if m:
                self.snap[int(m.group(1), 16)] = dict(self.regs)
            self.spill()
            self.emit('goto %s;' % args[0])
            return
        if op == 'call':
            name = args[0]
            name = FUNCS.get(name, name)
            n = None
            # find "add sp,N" right after
            if i + 1 < len(self.lines):
                m = re.match(r'add\s+sp,0x([0-9a-f]+)', self.lines[i + 1][1])
                if m:
                    n = int(m.group(1), 16) // 2
                elif re.match(r'(mov\s+sp,bp)', self.lines[i + 1][1]):
                    n = len(self.stack)
            if n is None:
                n = 0
            if name in ('_lshl', '_lshr'):
                lo, hi = self.regs.get('ax', 'ax'), self.regs.get('dx', 'dx')
                cnt = self.regs.get('cx', 'cl')
                val = lo if hi.startswith('SIGN(') or hi == '0' else 'LONG(%s, %s)' % (hi, lo)
                self.flush_regs()
                expr = '(%s %s %s)' % (val, '<<' if name == '_lshl' else '>>', cnt)
                self.regs['ax'] = expr
                self.regs['dx'] = expr + '_hi'
                return
            lost = n > len(self.stack)
            callargs = list(reversed(self.stack[len(self.stack) - n:])) if n else []
            if lost:
                callargs.append('/* ARGS LOST */')
            if n:
                del self.stack[len(self.stack) - n:]
            keep = {r: self.regs[r] for r in ('si', 'di') if r in self.regs}
            self.flush_regs()
            self.regs.update(keep)
            self.regs['ax'] = 'r_%s' % (len(self.out))
            self.regs['dx'] = 'r_%s_hi' % (len(self.out))
            self.emit('r_%d = %s(%s);' % (len(self.out), name, ', '.join(callargs)))
            return
        if op in ('ret', 'retf'):
            self.emit('return %s;' % self.regs.get('ax', 'ax'))
            return
        if op == 'stc' or op == 'clc' or op == 'nop' or op == 'cld':
            return
        self.emit('/* %s %s */' % (op, ', '.join(args)))


def extract(path, func):
    lines, on = [], False
    for ln in open(path):
        if re.match(r'^\S+:', ln):
            name = ln.split(':')[0]
            key = ln.split(';')[-1].strip() if ';' in ln else name
            if on:
                break
            if func in (name, key):
                on = True
            continue
        if on:
            m = re.match(r'\s+([0-9a-f]{4})\s+(?:[0-9a-f]{2} )*[0-9a-f]{2}\s+(.*)', ln)
            if m:
                lines.append((int(m.group(1), 16), m.group(2).strip()))
    return lines


if __name__ == '__main__':
    lines = extract(sys.argv[1], sys.argv[2])
    print('/* %s: %d instructions */' % (sys.argv[2], len(lines)))
    print(Lifter(lines).run())
