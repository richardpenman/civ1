"""Run functions of the original CIV.EXE in a CPU emulator (Unicorn), on a loaded save game.

This is the reference ("oracle") the C port is tested against: load the same .SVE/.MAP into
both, call the original function here and the C function in the port, compare the state.

    from oracle import Emu
    emu = Emu(os.path.join(ORIG, 'CIV.EXE'))
    emu.load_save(os.path.join(ORIG, 'CIVIL0.SVE'), os.path.join(ORIG, 'CIVIL0.MAP'))
    ax = emu.call('f_0d07_68f0', 52, 19, 0)        # tile yield
    sve = emu.save_bytes()                          # state as .SVE bytes

Graphics/sound/input routines are stubbed; the map page is emulated in Python.
Requires Unicorn: PYTHONPATH=.toolchain/root/usr/lib/python3/dist-packages
"""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ORIG = os.environ.get('CIV_ORIG') or os.path.expanduser('~/Dos/Civ1')   # the original game's files
TC = os.path.join(ROOT, '.toolchain/root/usr')
sys.path.insert(0, os.path.join(TC, 'lib/python3/dist-packages'))
import ctypes  # noqa: E402
_libuc = os.path.join(TC, 'lib/x86_64-linux-gnu/libunicorn.so.2')
if os.path.exists(_libuc):
    ctypes.CDLL(_libuc, mode=ctypes.RTLD_GLOBAL)
sys.path.insert(0, HERE)

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,  # noqa: E402
                               UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                               UC_X86_REG_IP)
from unexepack import unpack  # noqa: E402
import pic  # noqa: E402
from sve_layout import SVE_LAYOUT  # noqa: E402

BASE = 0x1000          # load segment of the root image
DGROUP = 0x2A1B
OVL_SEG = 0x2388
RET_SEG = 0xF000       # far return to here ends an emulated call
OVLRET_SEG = 0xF100    # far return through here reloads the caller's overlay
STUB_SEG = 0xF200      # stubbed routines return through a 'retf' here


def load_symbols():
    funcs = {}
    for ln in open(os.path.join(HERE, 'symbols.map')):
        parts = ln.split('#')[0].split()
        if len(parts) >= 2 and not parts[0].startswith('g_'):
            funcs[parts[1]] = parts[0]
    return funcs


def parse_addr(name, names):
    """'f_SSSS_OOOO' / 'o_NN_OOOO' / symbol name -> (kind, seg_or_ovl, off)."""
    name = names.get(name, name)
    m = re.fullmatch(r'f_([0-9a-f]{4})_([0-9a-f]{4})', name)
    if m:
        return 'root', int(m.group(1), 16), int(m.group(2), 16)
    m = re.fullmatch(r'o_(\d\d)_([0-9a-f]{4})', name)
    if m:
        return 'ovl', int(m.group(1)), int(m.group(2), 16)
    raise KeyError(name)


class Emu:
    def __init__(self, exe_path):
        raw = open(exe_path, 'rb').read()
        exe, relocs = unpack(raw)
        hdrp, = struct.unpack('<H', exe[8:10])
        last, pages = struct.unpack('<HH', exe[2:6])
        img = bytearray(exe[hdrp * 16:(pages - 1) * 512 + (last or 512)])
        for off, seg in relocs:
            a = seg * 16 + off
            v, = struct.unpack('<H', img[a:a + 2])
            img[a:a + 2] = struct.pack('<H', (v + BASE) & 0xFFFF)
        self.image = bytes(img)
        self.overlays = self._read_overlays(raw)
        self.names = load_symbols()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x100000)
        self.page = [bytearray(320 * 200) for _ in range(4)]
        self.loaded_ovl = None
        self.ovl_returns = {}
        self.stubs = {}            # linear address -> (name, return value or callable)
        self.ovl_stubs = {}        # overlay -> {linear address -> (name, result)}
        self.trace_calls = False
        self.unknown_calls = {}
        self.reset()
        self.uc.hook_add(UC_HOOK_INTR, self._on_intr)
        self._install_default_stubs()

    # ----- memory image ----------------------------------------------------------------
    def _read_overlays(self, raw):
        ovls = {}
        p = raw.find(b'MZ', 0x2b000)
        while p >= 0:
            last, pages, nrel, hdr = struct.unpack('<4H', raw[p + 2:p + 10])
            relo, num = struct.unpack('<HH', raw[p + 24:p + 28])
            size = (pages - 1) * 512 + (last or 512)
            code = bytearray(raw[p + hdr * 16:p + size])
            for i in range(nrel):
                off, seg = struct.unpack('<HH', raw[p + relo + 4 * i:p + relo + 4 * i + 4])
                a = (seg - OVL_SEG) * 16 + off
                v, = struct.unpack('<H', code[a:a + 2])
                code[a:a + 2] = struct.pack('<H', (v + BASE) & 0xFFFF)
            ovls[num] = bytes(code)
            p = raw.find(b'MZ', p + size)
        return ovls

    def reset(self):
        self.uc.mem_write(BASE * 16, self.image)
        self.uc.mem_write(BASE * 16 + len(self.image), b'\0' * (0x10000 - (len(self.image) - DGROUP * 16)))
        for seg in (RET_SEG, STUB_SEG):
            self.uc.mem_write(seg * 16, b'\xcb' * 256)   # retf
        self.uc.mem_write(OVLRET_SEG * 16, b'\xcd\xfd' * 128)   # int 0FDh: return into an overlay
        for addr in getattr(self, 'stubs', {}):
            self.uc.mem_write(addr, b'\xcd\xfe\xcb')
        for addr, data in getattr(self, 'patches', {}).items():
            self.uc.mem_write(addr, data)
        self.heap_seg = 0x9000
        self.loaded_ovl = None
        self.kb_toggle = False

    def patch(self, seg, off, old, new):
        """Replace code bytes in a root segment (kept across reset()).  Used where the
        original's behaviour is undefined and the port picks a definition."""
        addr = self.lin(seg, off)
        if self.image[addr - BASE * 16:addr - BASE * 16 + len(old)] != old:
            raise ValueError('patch %04x:%04x: unexpected code' % (seg, off))
        self.patches = getattr(self, 'patches', {})
        self.patches[addr] = new
        self.uc.mem_write(addr, new)

    def ovl_patch(self, ovl, off, new):
        """Replace code bytes in overlay `ovl` whenever it is loaded."""
        self.ovl_patches = getattr(self, 'ovl_patches', {})
        self.ovl_patches.setdefault(ovl, {})[off] = new
        if self.loaded_ovl == ovl:
            self.uc.mem_write(self.lin(OVL_SEG, off), new)

    def ovl_jump(self, ovl, off, target):
        """Patch a near jmp from off to target in overlay `ovl`."""
        self.ovl_patch(ovl, off, b'\xe9' + struct.pack('<h', target - (off + 3)))

    def lin(self, seg, off):
        return ((seg + BASE) * 16 + off) & 0xFFFFF

    def ds(self, off):
        return (DGROUP + BASE) * 16 + off

    def rd(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def rw(self, addr):
        return struct.unpack('<H', self.rd(addr, 2))[0]

    def rws(self, addr):
        return struct.unpack('<h', self.rd(addr, 2))[0]

    def ww(self, addr, v):
        self.uc.mem_write(addr, struct.pack('<H', v & 0xFFFF))

    def g(self, off):
        """Read a signed word global in DGROUP."""
        return self.rws(self.ds(off))

    def load_overlay(self, n):
        if self.loaded_ovl != n:
            self.uc.mem_write(self.lin(OVL_SEG, 0), self.overlays[n])
            for addr in self.ovl_stubs.get(n, {}):
                self.uc.mem_write(addr, b'\xcd\xfe\xcb')
            for off, code in getattr(self, 'ovl_patches', {}).get(n, {}).items():
                self.uc.mem_write(self.lin(OVL_SEG, off), code)
            # Unicorn may keep code translated from the previous overlay at these addresses.
            base = self.lin(OVL_SEG, 0)
            self.uc.ctl_remove_cache(base, base + 0x10000)
            self.loaded_ovl = n

    # ----- save games --------------------------------------------------------------------
    def _block_addr(self, seg, addr):
        if seg == 'ds':
            if addr.startswith('WORD PTR ds:'):
                return self.ds(self.rw(self.ds(int(addr[12:], 16))))
            return self.ds(int(addr, 16))
        return self.lin(int(seg, 16), int(addr, 16))

    def load_save(self, sve_path, map_path):
        data = open(sve_path, 'rb').read()
        pos = 0
        for seg, addr, size in SVE_LAYOUT:
            self.uc.mem_write(self._block_addr(seg, addr), data[pos:pos + size])
            pos += size
        _, _, (w, h, px) = pic.load(map_path)
        self.page[2][:] = px

    def save_bytes(self):
        out = bytearray()
        for seg, addr, size in SVE_LAYOUT:
            out += self.rd(self._block_addr(seg, addr), size)
        return bytes(out)

    # ----- calling -----------------------------------------------------------------------
    def func_addr(self, name):
        kind, seg, off = parse_addr(name, self.names)
        if kind == 'root':
            return BASE + seg, off, None
        return BASE + OVL_SEG, off, seg

    def call(self, name, *args, max_insns=0, timeout_s=60, si=0, di=0):
        cs, ip, ovl = self.func_addr(name)
        if ovl is not None:
            self.load_overlay(ovl)
        uc = self.uc
        dseg = BASE + DGROUP
        sp = 0xFFE0
        for a in reversed(args):
            sp -= 2
            self.ww(dseg * 16 + sp, a)
        sp -= 4
        self.ww(dseg * 16 + sp, 0)          # return IP
        self.ww(dseg * 16 + sp + 2, RET_SEG)  # return CS
        for reg, v in ((UC_X86_REG_SS, dseg), (UC_X86_REG_DS, dseg), (UC_X86_REG_ES, dseg),
                       (UC_X86_REG_SP, sp), (UC_X86_REG_BP, 0), (UC_X86_REG_CS, cs),
                       (UC_X86_REG_IP, ip), (UC_X86_REG_SI, si), (UC_X86_REG_DI, di)):
            uc.reg_write(reg, v)
        self.ovl_returns = {}
        try:
            uc.emu_start(cs * 16 + ip, RET_SEG * 16, timeout=int(timeout_s * 1e6), count=max_insns)
        except UcError as e:
            raise RuntimeError('emulation failed at %04x:%04x: %s' % (
                uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP), e))
        if uc.reg_read(UC_X86_REG_CS) != RET_SEG:
            raise RuntimeError('did not return (stopped at %04x:%04x) backtrace %s' % (
                uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP), self.backtrace()))
        ax = uc.reg_read(UC_X86_REG_AX)
        return ax - 0x10000 if ax & 0x8000 else ax

    def backtrace(self, depth=12):
        """Far return addresses found by walking the BP chain (MSC frames)."""
        uc = self.uc
        ss, bp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_BP)
        out = []
        for _ in range(depth):
            if not bp:
                break
            ip, cs = self.rw(ss * 16 + bp + 2), self.rw(ss * 16 + bp + 4)
            out.append('%04x:%04x' % ((cs - BASE) & 0xFFFF, ip))
            bp = self.rw(ss * 16 + bp)
        return ' < '.join(out)

    # ----- hooks -------------------------------------------------------------------------
    def _pop(self, n=1):
        uc = self.uc
        ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
        vals = [self.rw(ss * 16 + sp + 2 * i) for i in range(n)]
        uc.reg_write(UC_X86_REG_SP, sp + 2 * n)
        return vals

    def _push(self, *vals):
        uc = self.uc
        ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
        for v in vals:
            sp -= 2
            self.ww(ss * 16 + sp, v)
        uc.reg_write(UC_X86_REG_SP, sp)

    def _on_intr(self, uc, intno, data):
        # Unicorn calls this instead of delivering the interrupt: nothing has been pushed and
        # IP already points past the 'int' instruction.
        cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
        if intno == 0xFE:
            self._run_stub(cs * 16 + ip - 2)
            return
        if intno == 0xFD:
            ovl, rcs, rip = self.ovl_returns.pop((ip - 2 - 0) // 2 if cs == OVLRET_SEG else None)
            self.load_overlay(ovl)
            uc.reg_write(UC_X86_REG_CS, rcs)
            uc.reg_write(UC_X86_REG_IP, rip)
            return
        if intno == 0x21:
            self._dos(uc)
            return
        if intno == 0x16:                               # BIOS keyboard: Enter is always waiting
            ah = uc.reg_read(UC_X86_REG_AX) >> 8
            if ah in (0x00, 0x10):
                uc.reg_write(UC_X86_REG_AX, 0x1C0D)
            elif ah in (0x01, 0x11):
                uc.reg_write(UC_X86_REG_AX, 0x1C0D)
                from unicorn.x86_const import UC_X86_REG_FLAGS
                uc.reg_write(UC_X86_REG_FLAGS, uc.reg_read(UC_X86_REG_FLAGS) & ~0x40)
            elif ah == 0x02:
                uc.reg_write(UC_X86_REG_AX, 0)
            return
        if intno in (0x10, 0x33, 0x1A):                 # video BIOS, mouse, clock: ignore
            uc.reg_write(UC_X86_REG_AX, 0)
            return
        if intno != 0x3F:
            raise RuntimeError('unexpected int %02x at %04x:%04x' % (intno, cs, ip))
        site = cs * 16 + ip
        n = self.rd(site, 1)[0]
        off = self.rw(site + 1)
        if self.loaded_ovl is not None and self.loaded_ovl != n:
            # Code of the overlay about to be replaced may still have frames on the stack
            # (an overlay called the root, which calls this overlay): their returns go
            # through thunks that bring it back, as the real overlay manager arranges.
            bp = uc.reg_read(UC_X86_REG_BP)
            ss = uc.reg_read(UC_X86_REG_SS)
            seen = 0
            while bp and seen < 64:
                seen += 1
                rip, rcs = self.rw(ss * 16 + bp + 2), self.rw(ss * 16 + bp + 4)
                if rcs == BASE + OVL_SEG:
                    k = self._new_return(self.loaded_ovl, rcs, rip)
                    self.ww(ss * 16 + bp + 2, 2 * k)
                    self.ww(ss * 16 + bp + 4, OVLRET_SEG)
                nbp = self.rw(ss * 16 + bp)
                if nbp <= bp:
                    break
                bp = nbp
        if cs == BASE + OVL_SEG and self.loaded_ovl is not None and self.loaded_ovl != n:
            # caller lives in an overlay that is about to be replaced: return through a thunk
            k = self._new_return(self.loaded_ovl, cs, ip + 3)
            self._push(OVLRET_SEG, 2 * k)
        else:
            self._push(cs, ip + 3)
        self.load_overlay(n)
        uc.reg_write(UC_X86_REG_CS, BASE + OVL_SEG)
        uc.reg_write(UC_X86_REG_IP, off)

    def _new_return(self, ovl, cs, ip):
        """A slot of the return-thunk segment that reloads `ovl` and goes to cs:ip."""
        k = next(i for i in range(128) if i not in self.ovl_returns)
        self.ovl_returns[k] = (ovl, cs, ip)
        return k

    # ----- minimal DOS (int 21h): files are served read-only from orig/ ------------------------
    def _set_carry(self, uc, on):
        from unicorn.x86_const import UC_X86_REG_FLAGS
        f = uc.reg_read(UC_X86_REG_FLAGS)
        uc.reg_write(UC_X86_REG_FLAGS, (f | 1) if on else (f & ~1))

    def _dos(self, uc):
        ah = uc.reg_read(UC_X86_REG_AX) >> 8
        al = uc.reg_read(UC_X86_REG_AX) & 0xFF
        bx, cx, dx = (uc.reg_read(r) for r in (UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX))
        ds = uc.reg_read(UC_X86_REG_DS)
        if not hasattr(self, 'files'):
            self.files = {}
        if ah == 0x3D:                                  # open
            name = bytes(self.rd(ds * 16 + dx, 80)).split(b'\0')[0].decode('latin1')
            path = os.path.join(ORIG, os.path.basename(name.replace('\\', '/')).upper())
            if al & 3 or not os.path.exists(path):
                uc.reg_write(UC_X86_REG_AX, 2)
                self._set_carry(uc, True)
                return
            h = 5 + len(self.files)
            while h in self.files:
                h += 1
            self.files[h] = [open(path, 'rb').read(), 0]
            uc.reg_write(UC_X86_REG_AX, h)
            self._set_carry(uc, False)
        elif ah == 0x3E:                                # close
            self.files.pop(bx, None)
            self._set_carry(uc, False)
        elif ah == 0x3F:                                # read
            f = self.files[bx]
            chunk = f[0][f[1]:f[1] + cx]
            f[1] += len(chunk)
            uc.mem_write(ds * 16 + dx, chunk)
            uc.reg_write(UC_X86_REG_AX, len(chunk))
            self._set_carry(uc, False)
        elif ah == 0x42:                                # lseek
            f = self.files[bx]
            off = (cx << 16) | dx
            if off & 0x80000000:
                off -= 1 << 32
            f[1] = [0, f[1], len(f[0])][al] + off
            uc.reg_write(UC_X86_REG_AX, f[1] & 0xFFFF)
            uc.reg_write(UC_X86_REG_DX, f[1] >> 16)
            self._set_carry(uc, False)
        elif ah == 0x0B:                                # key available: every other time (Enter),
            self.kb_toggle = not getattr(self, 'kb_toggle', False)   # so flush loops end
            uc.reg_write(UC_X86_REG_AX, (ah << 8) | (0xFF if self.kb_toggle else 0))
        elif ah in (0x01, 0x07, 0x08):                  # read key: Enter
            uc.reg_write(UC_X86_REG_AX, (ah << 8) | 0x0D)
        elif ah == 0x2C:                                # time: fixed
            uc.reg_write(UC_X86_REG_CX, 0x0C00)
            uc.reg_write(UC_X86_REG_DX, 0)
        elif ah == 0x2A:                                # date: fixed
            uc.reg_write(UC_X86_REG_CX, 1991)
            uc.reg_write(UC_X86_REG_DX, 0x0C01)
        elif ah == 0x43:                                # file attributes (get)
            name = bytes(self.rd(ds * 16 + dx, 80)).split(b'\0')[0].decode('latin1')
            path = os.path.join(ORIG, os.path.basename(name.replace('\\', '/')).upper())
            if os.path.exists(path):
                uc.reg_write(UC_X86_REG_CX, 0x20)
                self._set_carry(uc, False)
            else:
                uc.reg_write(UC_X86_REG_AX, 2)
                self._set_carry(uc, True)
        elif ah == 0x4A:                                # resize memory block: fine
            self._set_carry(uc, False)
        elif ah == 0x48:                                # allocate: from a pool above the program
            self.heap_seg = getattr(self, 'heap_seg', 0x9000)
            if self.heap_seg + bx > 0xA000:
                uc.reg_write(UC_X86_REG_AX, 8)
                uc.reg_write(UC_X86_REG_BX, 0xA000 - self.heap_seg)
                self._set_carry(uc, True)
            else:
                uc.reg_write(UC_X86_REG_AX, self.heap_seg)
                self.heap_seg += bx
                self._set_carry(uc, False)
        elif ah == 0x49:                                # free
            self._set_carry(uc, False)
        elif ah == 0x44:                                # ioctl: not a device
            uc.reg_write(UC_X86_REG_DX, 0)
            self._set_carry(uc, False)
        else:
            raise RuntimeError('unsupported DOS call ah=%02x at %s (overlay %s)' % (ah, self.backtrace(6), self.loaded_ovl))

    def stub(self, name, result=0):
        """Make root function `name` return `result` (an int, or callable(emu, args_addr) -> int).
        The entry is patched to 'int 0FEh; retf'; the interrupt hook supplies AX."""
        cs, ip, ovl = self.func_addr(name)
        addr = cs * 16 + ip
        if ovl is not None:
            self.ovl_stubs.setdefault(ovl, {})[addr] = (name, result)
            if self.loaded_ovl == ovl:
                self.uc.mem_write(addr, b'\xcd\xfe\xcb')
            return
        self.stubs[addr] = (name, result)
        self.uc.mem_write(addr, b'\xcd\xfe\xcb')

    def _run_stub(self, addr):
        hit = self.stubs.get(addr) or self.ovl_stubs.get(self.loaded_ovl, {}).get(addr)
        if hit is None:
            ss, sp = self.uc.reg_read(UC_X86_REG_SS), self.uc.reg_read(UC_X86_REG_SP)
            raise RuntimeError('int 0FEh at %05x is no stub (overlay %s) %s stack %s' % (
                addr, self.loaded_ovl, self.backtrace(4), [hex(self.rw(ss * 16 + sp + 2 * i)) for i in range(6)]))
        name, result = hit
        uc = self.uc
        ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
        args_addr = ss * 16 + sp + 4
        v = result(self, args_addr) if callable(result) else result
        if self.trace_calls:
            print('stub', name, [self.rw(args_addr + 2 * i) for i in range(4)], '->', v)
        uc.reg_write(UC_X86_REG_AX, (v or 0) & 0xFFFF)

    def arg(self, args_addr, i):
        v = self.rw(args_addr + 2 * i)
        return v - 0x10000 if v & 0x8000 else v

    def _install_default_stubs(self):
        def get_pixel(emu, a):           # f_0000_07c3(page, x, y): linear, like the driver
            page, x, y = emu.arg(a, 0), emu.arg(a, 1), emu.arg(a, 2)
            k = y * 320 + x
            if 0 <= k < 64000 and 0 <= page < 4:
                return emu.page[page][k]
            return 0

        def put_pixel(emu, a):           # f_0000_1041(screen, x, y, colour)
            scr, x, y, c = emu.rw(a), emu.arg(a, 1), emu.arg(a, 2), emu.arg(a, 3)
            page = emu.rw(emu.ds(scr))
            k = y * 320 + x
            if 0 <= k < 64000 and 0 <= page < 4:
                emu.page[page][k] = c & 0xFF
            return 0
        self.ticks = 0

        def ticks(emu, a):               # f_0000_032c: timer ticks; advance so delays end
            emu.ticks = (emu.ticks + 1) & 0xFFFF
            return emu.ticks
        self.stub('f_0000_07c3', get_pixel)
        self.stub('f_0000_1041', put_pixel)
        self.stub('f_0000_032c', ticks)

        # Page-to-page drawing, used by the world generator on its work page.  Off unless
        # emu.gfx is set (elsewhere these only draw the screen).
        self.gfx = False

        def page_of(emu, scr):
            return emu.rw(emu.ds(scr))

        def setp(emu, page, x, y, c):
            k = y * 320 + x
            if 0 <= k < 64000 and 0 <= page < 4:
                emu.page[page][k] = c & 0xFF

        def getp(emu, page, x, y):
            k = y * 320 + x
            return emu.page[page][k] if 0 <= k < 64000 and 0 <= page < 4 else 0

        def blit(emu, a):                # f_0000_07ed(src, sx, sy, w, h, dst, dx, dy)
            if not emu.gfx:
                return 0
            src, sx, sy, w, h, dst, dx, dy = (emu.arg(a, i) for i in range(8))
            sp, dp = page_of(emu, src & 0xFFFF), page_of(emu, dst & 0xFFFF)
            tmp = [[getp(emu, sp, sx + i, sy + j) for i in range(w)] for j in range(h)]
            for j in range(h):
                for i in range(w):
                    setp(emu, dp, dx + i, dy + j, tmp[j][i])
            return 0

        def fill(emu, a):                # f_0000_0bec(scr, x, y, w, h, colour)
            if not emu.gfx:
                return 0
            scr, x, y, w, h, c = (emu.arg(a, i) for i in range(6))
            pg = page_of(emu, scr & 0xFFFF)
            for j in range(h):
                for i in range(w):
                    setp(emu, pg, x + i, y + j, c)
            return 0

        def line(emu, a):                # f_0000_0ffc(scr, x1, y1, x2, y2, colour): straight lines
            if not emu.gfx:
                return 0
            scr, x1, y1, x2, y2, c = (emu.arg(a, i) for i in range(6))
            pg = page_of(emu, scr & 0xFFFF)
            if x1 != x2 and y1 != y2:
                raise RuntimeError('diagonal line not emulated')
            for x in range(min(x1, x2), max(x1, x2) + 1):
                for y in range(min(y1, y2), max(y1, y2) + 1):
                    setp(emu, pg, x, y, c)
            return 0
        def load_pic(emu, a):            # f_1fb4_0224(page, name): a picture into a page
            if not emu.gfx:
                return 0
            import pic
            page, ptr = emu.arg(a, 0), emu.rw(a + 2)
            name = bytes(emu.rd(emu.ds(ptr), 40)).split(b'\0')[0].decode('latin1')
            _, _, img = pic.load(os.path.join(ORIG, name.upper()))
            w, h, pix = img
            emu.page[page][:len(pix)] = pix
            return 0
        self.stub('f_1fb4_0224', load_pic)

        def text_section(emu, a):        # load_text_section("*KEY"): KING.TXT's section into
            ptr = emu.rw(a)              # the message buffer (the original uses stdio, whose
            key = bytes(emu.rd(emu.ds(ptr), 40)).split(b'\0')[0]   # heap the oracle lacks)
            lines = open(os.path.join(ORIG, 'KING.TXT'), 'rb').read().split(b'\r\n')
            buf = bytes(emu.rd(emu.ds(0xc926), 1024)).split(b'\0')[0]
            if key in lines:
                i = lines.index(key) + 1
                while i < len(lines) and not lines[i].startswith(b'*'):
                    buf += lines[i] + b'\n'
                    i += 1
            emu.uc.mem_write(emu.ds(0xc926), buf[:1023] + b'\0')
            return 0
        self.stub('load_text_section', text_section)
        self.stub('f_0000_07ed', blit)
        self.stub('f_0000_0bec', fill)
        self.stub('f_0000_0ffc', line)
        # User interface: dialogs/menus answer 0, full-screen displays are skipped.
        # dialog engine; palace screen (the human builds it: M4); autosave
        for name in ('f_1d1e_003f', 'o_11_0368'):
            self.stub(name, 0)

        def article(emu, a):             # o_08_062a: only its border's rnd(2) (f_0860_25e1)
            s = emu.rw(emu.ds(0x5bda)) | emu.rw(emu.ds(0x5bdc)) << 16
            s = (s * 214013 + 2531011) & 0xFFFFFFFF          # one MSC rand()
            emu.ww(emu.ds(0x5bda), s)
            emu.ww(emu.ds(0x5bdc), s >> 16)
            return 0
        self.stub('o_08_062a', article)
        # Overlays that only display things: 19 city view / wonder movies, 21 newspaper.  Their
        # entry points run up to their last random number (the city view re-seeds the generator
        # and lays the city out with it; the newspaper draws its crowd, name and headline) and
        # return there; the rest only draws.
        for ln in open(os.path.join(ROOT, 'build/asm/symbols.txt')):
            key = ln.split()[0]
            if key.startswith(('o_19_', 'o_21_')) and key not in ('o_19_0000', 'o_21_0000'):
                self.stub(key, 0)
        self.ovl_jump(19, 0x08e4, 0x0fdf)
        self.ovl_jump(21, 0x03b0, 0x0424)
        # The palace screen (o_17_0000): no part chosen; it still redraws the map at the end.
        self.ovl_jump(17, 0x0009, 0x0792)
        # Everything else in seg 0000 is graphics-driver / timer glue.
        for ln in open(os.path.join(ROOT, 'build/asm/symbols.txt')):
            key = ln.split()[0]
            if key.startswith('f_0000_') and key not in ('f_0000_07c3', 'f_0000_1041', 'f_0000_032c',
                                                         'f_0000_07ed', 'f_0000_0bec', 'f_0000_0ffc'):
                self.stub(key, 0)


if __name__ == '__main__':
    emu = Emu(os.path.join(ORIG, 'CIV.EXE'))
    emu.load_save(os.path.join(ORIG, 'CIVIL0.SVE'), os.path.join(ORIG, 'CIVIL0.MAP'))
    assert emu.save_bytes() == open(os.path.join(ORIG, 'CIVIL0.SVE'), 'rb').read()
    print('terrain(52,19) =', emu.call('map_terrain', 52, 19))
    print('yield(52,19) food/shield/trade =', [emu.call('f_0d07_68f0', 52, 19, k) for k in range(3)])
    print('civ 7 has advance 0:', emu.call('civ_has_advance', 7, 0))
