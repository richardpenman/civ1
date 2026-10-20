"""A small DOS PC in Unicorn that runs the whole original game (CIV.EXE as shipped).

    python3 -I tools/dosmachine.py [--keys "..."] [--shots DIR]

DOS is emulated at the int 21h level with files served from ORIG (~/Dos/Civ1 or $CIV_ORIG) (writes go to a scratch
directory); the BIOS keyboard, timer tick, video mode and mouse (int 33h) are emulated; the
VGA is mode 13h memory at A000 plus the DAC palette ports.  Time is virtual: the timer
interrupt fires every TICK_INSNS instructions, and the clock reads a fixed date, so a run is
deterministic.  Input comes from a script; screenshots are taken whenever the program asks
for input with nothing queued.  The front end harness (tools/lockstep.py) drives this and the
SDL port with the same script and compares them.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ORIG = os.environ.get('CIV_ORIG') or os.path.expanduser('~/Dos/Civ1')   # the original game's files
TC = os.path.join(ROOT, '.toolchain/root/usr')
sys.path.insert(0, os.path.join(TC, 'lib/python3/dist-packages'))
import ctypes  # noqa: E402
_libuc = os.path.join(TC, 'lib/x86_64-linux-gnu/libunicorn.so.2')
if os.path.exists(_libuc):
    ctypes.CDLL(_libuc, mode=ctypes.RTLD_GLOBAL)
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_INSN, UcError  # noqa: E402
from unicorn.x86_const import *  # noqa: E402,F401,F403

PSP_SEG = 0x1000          # program segment prefix: all code above 64K (EXEPACK and the IP check need that)
ENV_SEG = 0x0FE0
HEAP_TOP = 0xA000         # conventional memory ends at A000 (VGA)
COUNTS_PER_INSN = 3       # PIT input clocks (1.193 MHz) per emulated instruction (a ~4 MHz-ish PC)
RETRACE = 17045           # PIT clocks per VGA frame (70 Hz)
WAIT_CLOCKS = 30 * 1193182  # polling the keyboard this long (30 s: longer than any animation) = waiting


class Halt(Exception):
    pass


class DosMachine:
    def __init__(self, data_dir=ORIG, scratch=None, verbose=False):
        self.data_dir = data_dir
        self.scratch = scratch or os.path.join(ROOT, 'build', 'dos_scratch')
        os.makedirs(self.scratch, exist_ok=True)
        self.verbose = verbose
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        # interrupt vector table: every vector points at an 'int 0xFF; iret'-style trap in
        # our BIOS segment, so a program's own handlers chained through it come back to us
        self.bios_seg = 0xF000
        for v in range(256):
            uc.mem_write(self.bios_seg * 16 + v * 4, bytes([0xCD, v if v != 0xFF else 0xFE, 0xCF, 0x90]))
            self.ww(v * 4, v * 4)
            self.ww(v * 4 + 2, self.bios_seg)
        self.files = {}
        self.drivers = {}             # driver file -> load segment
        self.mem_next = HEAP_TOP      # set after the program is loaded
        self.blocks = {}
        self.dac = bytearray(768)
        self.dac_index = 0
        self.dac_read = 0
        self.keys = []                # BIOS keyboard queue: (scan << 8 | ascii)
        self.mouse = [160, 100, 0]    # x, y, buttons
        self.ticks = 0
        self.pit0 = 0x10000           # channel 0 reload value (the timer interrupt period)
        self.vclock = 0               # PIT clocks since start
        self.pit_latch = None
        self.pit_lohi = 0
        self.poll_since = None        # vclock of the first unanswered keyboard poll
        self.on_wait = None           # called when the program waits for input
        self.stop_reason = None       # 'wait' (for input) or 'mouse' (the handler returned)
        self.video_mode = 3
        self.insns = 0

    # ---- memory helpers ----------------------------------------------------------------
    def rd(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def rw(self, addr):
        return struct.unpack('<H', self.rd(addr, 2))[0]

    def ww(self, addr, v):
        self.uc.mem_write(addr, struct.pack('<H', v & 0xFFFF))

    def reg(self, r):
        return self.uc.reg_read(r)

    def set(self, r, v):
        self.uc.reg_write(r, v & 0xFFFF)

    def cstr(self, seg, off):
        s = self.rd(seg * 16 + off, 128)
        return s.split(b'\0')[0].decode('latin1')

    def carry(self, on):
        f = self.reg(UC_X86_REG_FLAGS)
        self.uc.reg_write(UC_X86_REG_FLAGS, (f | 1) if on else (f & ~1))

    def zero(self, on):
        f = self.reg(UC_X86_REG_FLAGS)
        self.uc.reg_write(UC_X86_REG_FLAGS, (f | 0x40) if on else (f & ~0x40))

    # ---- loading -----------------------------------------------------------------------
    def load_exe(self, name, args=''):
        data = open(os.path.join(self.data_dir, name), 'rb').read()
        if data[0x1C:0x1C + 2] != b'\0\0' or b'Packed file is corrupt' in data[:0x20000]:
            # EXEPACK: unpack it the way its stub would (the stub itself misbehaves here);
            # the overlay manager still reads the overlays from the file on "disk"
            from unexepack import unpack
            data, _ = unpack(data)
        hdr = struct.unpack('<14H', data[:28])
        hdr_size = hdr[4] * 16
        size = (hdr[2] - 1) * 512 + (hdr[1] or 512) if hdr[2] else len(data)
        image = data[hdr_size:size]
        load = PSP_SEG + 0x10
        self.uc.mem_write(load * 16, image)
        for i in range(hdr[3]):
            off, seg = struct.unpack_from('<HH', data, hdr[12] + 4 * i)
            a = (load + seg) * 16 + off
            self.ww(a, self.rw(a) + load)
        # PSP
        psp = PSP_SEG * 16
        self.uc.mem_write(psp, b'\xcd\x20')
        self.ww(psp + 2, HEAP_TOP)
        self.ww(psp + 0x2C, ENV_SEG)
        tail = (' ' + args if args else '').encode()
        self.uc.mem_write(psp + 0x80, bytes([len(tail)]) + tail + b'\r')
        env = b'PATH=C:\\\0COMSPEC=C:\\COMMAND.COM\0\0\x01\0C:\\' + name.upper().encode() + b'\0'
        self.uc.mem_write(ENV_SEG * 16, env)
        self.exe_name = name.upper()
        # memory above the program image (minalloc paragraphs) is the heap start
        prog_end = load + (len(image) + 15) // 16 + hdr[5]
        self.blocks = {PSP_SEG: HEAP_TOP - PSP_SEG}
        self.mem_next = prog_end
        ss, sp, ip, cs = load + hdr[7], hdr[8], hdr[10], load + hdr[11]
        for r, v in ((UC_X86_REG_SS, ss), (UC_X86_REG_SP, sp), (UC_X86_REG_CS, cs), (UC_X86_REG_IP, ip),
                     (UC_X86_REG_DS, PSP_SEG), (UC_X86_REG_ES, PSP_SEG)):
            self.set(r, v)

    # ---- running -----------------------------------------------------------------------
    def run(self, max_insns=10 ** 9):
        """Run until the timer interrupt is due, deliver it, repeat."""
        done = 0
        while done < max_insns:
            self._fix_ip()
            cs, ip = self.reg(UC_X86_REG_CS), self.reg(UC_X86_REG_IP)
            # the instructions until the timer counter next wraps
            due = self.pit0 - self.vclock % self.pit0
            n = max(1, due // COUNTS_PER_INSN)
            start = self.vclock
            try:
                self.uc.emu_start(cs * 16 + ip, 0x10FFFF, count=n)
            except UcError as e:
                raise RuntimeError('emulation failed at %04x:%04x: %s' % (
                    self.reg(UC_X86_REG_CS), self.reg(UC_X86_REG_IP), e))
            except Halt:
                return
            if self.stop_reason:             # stopped for input (part of the slice ran)
                reason, self.stop_reason = self.stop_reason, None
                self.vclock = start + due // 2
                self.insns += n // 2
                self._fix_ip()
                return reason
            self.vclock = start + due        # (port reads may have advanced it a little)
            done += n
            self.insns += n
            self._fix_ip()
            self._timer()

    def _fix_ip(self):
        """Unicorn leaves the linear address in EIP when a slice ends just after a far jump or
        call; all code here lies above 64K, so such a value is recognisable."""
        cs, eip = self.reg(UC_X86_REG_CS), self.uc.reg_read(UC_X86_REG_EIP)
        if eip > 0xFFFF:
            self.set(UC_X86_REG_IP, (eip - cs * 16) & 0xFFFF)

    def _bios_tick(self):
        """The BIOS timer interrupt: the tick count and the floppy motor countdown."""
        self.ticks += 1
        self.ww(0x46C, self.ticks & 0xFFFF)
        self.ww(0x46E, self.ticks >> 16)
        motor = self.rd(0x440, 1)[0]
        if motor:
            self.uc.mem_write(0x440, bytes([motor - 1]))

    def _push(self, v):
        sp = (self.reg(UC_X86_REG_SP) - 2) & 0xFFFF
        self.set(UC_X86_REG_SP, sp)
        self.ww(self.reg(UC_X86_REG_SS) * 16 + sp, v)

    def _timer(self):
        """Deliver IRQ0 (int 8) if interrupts are enabled."""
        flags = self.reg(UC_X86_REG_FLAGS)
        if not flags & 0x200 or getattr(self, 'irq0_in_service', False):
            return                       # (the PIC holds IRQ0 until the handler's EOI)
        vec_off, vec_seg = self.rw(8 * 4), self.rw(8 * 4 + 2)
        if vec_seg == self.bios_seg:
            self._bios_tick()
            return
        self.irq0_in_service = True
        self._push(flags)
        self._push(self.reg(UC_X86_REG_CS))
        self._push(self.reg(UC_X86_REG_IP))
        self.uc.reg_write(UC_X86_REG_FLAGS, flags & ~0x200)
        self.set(UC_X86_REG_CS, vec_seg)
        self.set(UC_X86_REG_IP, vec_off)

    # ---- ports -------------------------------------------------------------------------
    def _in(self, uc, port, size, data):
        self.vclock += 4 * COUNTS_PER_INSN       # (a polling loop's few instructions)
        if port == 0x3DA:                 # VGA status: vertical retrace from the clock
            return 0x09 if self.vclock % RETRACE < 1400 else 0
        if port == 0x40:                  # channel 0 counter (latched by 0x43)
            if self.pit_latch is None:
                self.pit_latch = self.pit0 - self.vclock % self.pit0
                self.pit_lohi = 0
            v = (self.pit_latch >> (8 * self.pit_lohi)) & 0xFF
            self.pit_lohi += 1
            if self.pit_lohi == 2:
                self.pit_latch = None
            return v
        if port == 0x3C9:
            v = self.dac[self.dac_read % 768]
            self.dac_read += 1
            return v
        if port == 0x60:
            return 0
        if port == 0x61:
            return 0
        return 0

    def _out(self, uc, port, size, value, data):
        value &= 0xFF
        if port == 0x20 and value == 0x20:                # end of interrupt
            self.irq0_in_service = False
        elif port == 0x43:
            if value & 0xC0 == 0 and value & 0x30 == 0:  # latch channel 0
                self.pit_latch = self.pit0 - self.vclock % self.pit0
                self.pit_lohi = 0
            elif value & 0xC0 == 0:                       # channel 0 mode: reload lo, hi next
                self.pit_write = []
        elif port == 0x40:
            w = getattr(self, 'pit_write', None)
            if w is not None:
                w.append(value)
                if len(w) == 2:
                    self.pit0 = (w[0] | w[1] << 8) or 0x10000
                    self.pit_write = None
        elif port == 0x3C8:
            self.dac_index = value * 3
        elif port == 0x3C7:
            self.dac_read = value * 3
        elif port == 0x3C9:
            self.dac[self.dac_index % 768] = value & 0x3F
            self.dac_index += 1

    # ---- interrupts --------------------------------------------------------------------
    def _intr(self, uc, intno, data):
        # Unicorn reports 'int n' with IP after the instruction.  Our IVT traps execute
        # 'int n' in the BIOS segment; a direct 'int n' in the program comes here too.
        ah = self.reg(UC_X86_REG_AX) >> 8
        cs = self.reg(UC_X86_REG_CS)
        if cs != self.bios_seg and self.rw(intno * 4 + 2) != self.bios_seg:
            # the program has its own handler: deliver the interrupt to it
            flags = self.reg(UC_X86_REG_FLAGS)
            self._push(flags)
            self._push(cs)
            self._push(self.reg(UC_X86_REG_IP))
            self.uc.reg_write(UC_X86_REG_FLAGS, flags & ~0x300)
            self.set(UC_X86_REG_CS, self.rw(intno * 4 + 2))
            self.set(UC_X86_REG_IP, self.rw(intno * 4))
            return
        if intno == 0x21:
            self._dos(ah)
        elif intno == 0x10:
            self._video(ah)
        elif intno == 0x16:
            self._keyboard(ah)
        elif intno == 0x1A:
            if ah == 0:
                self.set(UC_X86_REG_CX, self.ticks >> 16)
                self.set(UC_X86_REG_DX, self.ticks)
                self.set(UC_X86_REG_AX, 0)
        elif intno == 0x33:
            self._mouse(self.reg(UC_X86_REG_AX))
        elif intno == 0xF1 and cs == self.bios_seg:
            self._mouse_return()
        elif intno == 0x20:
            raise Halt()
        elif intno == 0x08:
            self._bios_tick()
            self.irq0_in_service = False
        elif intno in (0x09, 0x1C, 0x28, 0x2F, 0x15, 0x11, 0x12):
            if intno == 0x11:
                self.set(UC_X86_REG_AX, 0x0026)
            if intno == 0x12:
                self.set(UC_X86_REG_AX, 640)
        elif intno == 0x3F or intno == 0x00 or intno == 0x06:
            raise RuntimeError('unhandled int %02x at %04x:%04x' % (intno, self.reg(UC_X86_REG_CS),
                                                                   self.reg(UC_X86_REG_IP)))
        else:
            if self.verbose:
                print('int %02x ah=%02x ignored' % (intno, ah), file=sys.stderr)

    def _text_put(self, ch, attr=None):
        r, c = self.cursor
        a = 0xB8000 + (r * 80 + c) * 2
        if attr is None:
            self.uc.mem_write(a, bytes([ch]))
        else:
            self.uc.mem_write(a, bytes([ch, attr]))

    def text_screen(self):
        t = self.rd(0xB8000, 80 * 25 * 2)
        return '\n'.join(''.join(chr(t[(r * 80 + c) * 2]) if 32 <= t[(r * 80 + c) * 2] < 127 else ' '
                                  for c in range(80)).rstrip() for r in range(25))

    def _video(self, ah):
        if not hasattr(self, 'cursor'):
            self.cursor = [0, 0]
        al = self.reg(UC_X86_REG_AX) & 0xFF
        if ah == 0x02:
            dx = self.reg(UC_X86_REG_DX)
            self.cursor = [min(dx >> 8, 24), min(dx & 0xFF, 79)]
        elif ah == 0x03:
            self.set(UC_X86_REG_DX, self.cursor[0] << 8 | self.cursor[1])
        elif ah in (0x09, 0x0A):
            r, c = self.cursor
            for i in range(self.reg(UC_X86_REG_CX)):
                p = r * 80 + c + i
                if p >= 2000:
                    break
                self.uc.mem_write(0xB8000 + p * 2, bytes([al]) if ah == 0x0A else
                                  bytes([al, self.reg(UC_X86_REG_BX) & 0xFF]))
        elif ah == 0x0E:
            r, c = self.cursor
            if al == 13:
                c = 0
            elif al == 10:
                r += 1
            elif al == 8:
                c = max(c - 1, 0)
            else:
                self._text_put(al)
                c += 1
                if c >= 80:
                    c, r = 0, r + 1
            if r >= 25:
                self.uc.mem_write(0xB8000, self.rd(0xB8000 + 160, 160 * 24) + b' \x07' * 80)
                r = 24
            self.cursor = [r, c]
        elif ah == 0x06 and al == 0:
            self.uc.mem_write(0xB8000, b' \x07' * 2000)
        if ah == 0x00:
            self.video_mode = self.reg(UC_X86_REG_AX) & 0x7F
            self.mode_flag = self.reg(UC_X86_REG_AX) & 0x80        # "don't clear", reported back
            if self.video_mode == 0x13 and not self.mode_flag:
                self.uc.mem_write(0xA0000, bytes(64000))
            if self.video_mode == 3:
                self.uc.mem_write(0xB8000, b' \x07' * 2000)
            self.cursor = [0, 0]
        elif ah == 0x0F:
            self.set(UC_X86_REG_AX, (40 << 8) | self.video_mode | getattr(self, 'mode_flag', 0))
            self.set(UC_X86_REG_BX, self.reg(UC_X86_REG_BX) & 0xFF)        # BH = page 0
        elif ah == 0x1A:
            self.set(UC_X86_REG_AX, 0x001A)        # VGA present
            self.set(UC_X86_REG_BX, 0x0008)
        elif ah == 0x12:
            self.set(UC_X86_REG_BX, 0x0003)        # EGA/VGA with 256K
            self.set(UC_X86_REG_CX, 0x0009)
        elif ah == 0x10:
            al = self.reg(UC_X86_REG_AX) & 0xFF
            if al == 0x12:                         # set block of DAC registers
                first, n = self.reg(UC_X86_REG_BX), self.reg(UC_X86_REG_CX)
                src = self.reg(UC_X86_REG_ES) * 16 + self.reg(UC_X86_REG_DX)
                block = self.rd(src, n * 3)
                for i in range(n * 3):
                    self.dac[(first * 3 + i) % 768] = block[i] & 0x3F
            elif al == 0x10:
                i = self.reg(UC_X86_REG_BX) * 3
                dh, cx = self.reg(UC_X86_REG_DX) >> 8, self.reg(UC_X86_REG_CX)
                self.dac[i % 768:i % 768 + 3] = bytes([dh & 0x3F, (cx >> 8) & 0x3F, cx & 0x3F])

    def _keyboard(self, ah):
        if ah in (0x00, 0x10):
            if not self.keys and self._wait():
                return
            k = self.keys.pop(0) if self.keys else 0x1C0D
            self.poll_since = None
            self.set(UC_X86_REG_AX, k)
        elif ah in (0x01, 0x11):
            if not self.keys:
                if self._polled():
                    return
            if self.keys:
                self.set(UC_X86_REG_AX, self.keys[0])
                self.zero(False)
            else:
                self.zero(True)
        elif ah == 0x02:
            self.set(UC_X86_REG_AX, 0)

    def _polled(self):
        """A keyboard poll found nothing.  Animations poll too (to be skipped), so the
        program counts as waiting for input once it has polled for WAIT_CLOCKS of
        virtual time with no input given."""
        if self._flushing():
            self.poll_since = None
            return False
        if self.poll_since is None:
            self.poll_since = self.vclock
        elif self.vclock - self.poll_since > WAIT_CLOCKS:
            self.poll_since = None
            return self._wait()
        return False

    FLUSH = (0x0402, 0x44f5, 0x4512)    # f_0402_44f5: empties the keyboard buffer (not a wait)

    def _flushing(self):
        """Is the poll kbhit's, called from the keyboard flush?  (The far return address of
        CIV.EXE's kbhit lies at SS:SP when it polls.)"""
        ss, sp = self.reg(UC_X86_REG_SS), self.reg(UC_X86_REG_SP)
        ip, cs = self.rw(ss * 16 + sp), self.rw(ss * 16 + sp + 2)
        seg, lo, hi = self.FLUSH
        return cs == self.LOAD + seg and lo <= ip < hi

    def _wait(self):
        """The program waits for input with none queued: call on_wait (which queues some or
        raises Halt), or without one stop the run with the int to be issued again on resume
        (True: the caller returns at once)."""
        if self.on_wait:
            self.on_wait(self)
            return False
        self.set(UC_X86_REG_IP, self.reg(UC_X86_REG_IP) - 2)
        self.stop_reason = 'wait'
        self.uc.emu_stop()
        return True

    def _mouse(self, ax):
        if ax == 0:
            self.set(UC_X86_REG_AX, 0xFFFF)
            self.set(UC_X86_REG_BX, 2)
        elif ax == 0x0C:                  # the program's event handler (the game's only use)
            self.mouse_handler = (self.reg(UC_X86_REG_ES), self.reg(UC_X86_REG_DX), self.reg(UC_X86_REG_CX))
        elif ax == 3:
            self.set(UC_X86_REG_BX, self.mouse[2])
            self.set(UC_X86_REG_CX, self.mouse[0] * 2)
            self.set(UC_X86_REG_DX, self.mouse[1])

    MOUSE_RET = 0x500                 # 'int 0xF1' in the BIOS segment: the handler returned

    def mouse_event(self, x, y, buttons, mask):
        """Call the program's mouse handler as a driver would (x, y in 320x200 pixels;
        mask: 1 moved, 2/4 left pressed/released, 8/16 right pressed/released)."""
        self.mouse = [x, y, buttons]
        self.poll_since = None
        h = getattr(self, 'mouse_handler', None)
        if not h or not mask & h[2]:
            return
        saved = {r: self.uc.reg_read(globals()['UC_X86_REG_' + r]) for r in self._REGS}
        self.uc.mem_write(self.bios_seg * 16 + self.MOUSE_RET, b'\xcd\xf1')
        self._push(self.bios_seg)
        self._push(self.MOUSE_RET)
        for r, v in ((UC_X86_REG_AX, mask), (UC_X86_REG_BX, buttons), (UC_X86_REG_CX, x * 2),
                     (UC_X86_REG_DX, y), (UC_X86_REG_SI, 0), (UC_X86_REG_DI, 0),
                     (UC_X86_REG_CS, h[0]), (UC_X86_REG_IP, h[1])):
            self.set(r, v)
        self.uc.reg_write(UC_X86_REG_FLAGS, saved['EFLAGS'] & ~0x200)
        self.mouse_saved = saved
        if self.run(50_000_000) != 'mouse':
            raise RuntimeError('the mouse handler did not return')

    def _mouse_return(self):
        for r, v in self.mouse_saved.items():
            self.uc.reg_write(globals()['UC_X86_REG_' + r], v)
        self.stop_reason = 'mouse'
        self.uc.emu_stop()

    # ---- DOS ---------------------------------------------------------------------------
    def _path(self, name):
        base = name.replace('\\', '/').split('/')[-1].upper()
        if len(base) > 2 and base[1] == ':':
            base = base[2:]                            # "C:CIVIL0.SVE"
        for d in (self.scratch, self.data_dir):
            for f in os.listdir(d):
                if f.upper() == base:
                    return os.path.join(d, f)
        return None

    def _free_handle(self):
        h = 5
        while h in self.files:
            h += 1
        return h

    def _dos(self, ah):
        al = self.reg(UC_X86_REG_AX) & 0xFF
        bx, cx, dx = self.reg(UC_X86_REG_BX), self.reg(UC_X86_REG_CX), self.reg(UC_X86_REG_DX)
        ds, es = self.reg(UC_X86_REG_DS), self.reg(UC_X86_REG_ES)
        self.carry(False)
        if ah == 0x30:
            self.set(UC_X86_REG_AX, 0x0005)            # DOS 5.0
        elif ah == 0x25:
            self.ww(al * 4, dx)
            self.ww(al * 4 + 2, ds)
        elif ah == 0x35:
            self.set(UC_X86_REG_BX, self.rw(al * 4))
            self.set(UC_X86_REG_ES, self.rw(al * 4 + 2))
        elif ah == 0x4A:                               # resize block
            seg = es
            top = min([b for b in self.blocks if b > seg] + [HEAP_TOP])
            if seg + bx > top:
                self.carry(True)
                self.set(UC_X86_REG_AX, 8)
                self.set(UC_X86_REG_BX, top - seg)
            else:
                self.blocks[seg] = bx
        elif ah == 0x48:                               # allocate (first fit)
            best_gap, at = 0, None
            start = None
            for b in sorted(self.blocks):
                end = b + self.blocks[b]
                start = end if start is None or end > start else start
            # walk the gaps between blocks
            prev_end, gaps = None, []
            for b in sorted(self.blocks):
                if prev_end is not None and b > prev_end:
                    gaps.append((prev_end, b - prev_end))
                prev_end = max(prev_end or 0, b + self.blocks[b])
            gaps.append((prev_end, HEAP_TOP - prev_end))
            for g, n in gaps:
                if n >= bx and at is None:
                    at = g
                best_gap = max(best_gap, n)
            if at is None or bx == 0xFFFF:
                self.carry(True)
                self.set(UC_X86_REG_AX, 8)
                self.set(UC_X86_REG_BX, best_gap)
            else:
                self.blocks[at] = bx
                self.set(UC_X86_REG_AX, at)
        elif ah == 0x49:
            self.blocks.pop(es, None)
        elif ah == 0x3D:                               # open
            name = self.cstr(ds, dx)
            path = self._path(name)
            if not path:
                self.carry(True)
                self.set(UC_X86_REG_AX, 2)
            else:
                h = self._free_handle()
                base = name.replace('\\', '/').split('/')[-1].upper()
                if len(base) > 2 and base[1] == ':':
                    base = base[2:]
                self.files[h] = {'data': bytearray(open(path, 'rb').read()), 'pos': 0,
                                 'out': os.path.join(self.scratch, base) if al & 3 else None}
                self.set(UC_X86_REG_AX, h)
        elif ah == 0x3C:                               # create (writes go to scratch)
            name = self.cstr(ds, dx).replace('\\', '/').split('/')[-1].upper()
            if len(name) > 2 and name[1] == ':':
                name = name[2:]
            h = self._free_handle()
            self.files[h] = {'data': bytearray(), 'pos': 0, 'out': os.path.join(self.scratch, name)}
            open(self.files[h]['out'], 'wb').close()
            self.set(UC_X86_REG_AX, h)
        elif ah == 0x3E:
            f = self.files.pop(bx, None)
            if f and f['out']:
                with open(f['out'], 'wb') as w:
                    w.write(f['data'])
        elif ah == 0x3F:                               # read
            f = self.files.get(bx)
            if not f:
                if bx == 0:
                    self.set(UC_X86_REG_AX, 0)
                    return
                self.carry(True)
                self.set(UC_X86_REG_AX, 6)
                return
            chunk = bytes(f['data'][f['pos']:f['pos'] + cx])
            self.uc.mem_write(ds * 16 + dx, chunk)
            if chunk:       # (code may be read in: Unicorn would keep running the old translation)
                self.uc.ctl_remove_cache(ds * 16 + dx, ds * 16 + dx + len(chunk))
            f['pos'] += len(chunk)
            self.set(UC_X86_REG_AX, len(chunk))
        elif ah == 0x40:                               # write
            data = self.rd(ds * 16 + dx, cx)
            if bx in (1, 2):
                sys.stderr.write(data.decode('latin1'))
            else:
                f = self.files.get(bx)
                if not f:
                    self.carry(True)
                    self.set(UC_X86_REG_AX, 6)
                    return
                d, p = f['data'], f['pos']
                if p > len(d):
                    d.extend(bytes(p - len(d)))
                d[p:p + len(data)] = data
                f['pos'] = p + len(data)
            self.set(UC_X86_REG_AX, cx)
        elif ah == 0x42:                               # seek
            f = self.files.get(bx)
            if not f:
                self.carry(True)
                self.set(UC_X86_REG_AX, 6)
                return
            off = (cx << 16) | dx
            if off & 0x80000000:
                off -= 1 << 32
            pos = (0, f['pos'], len(f['data']))[al] + off
            f['pos'] = pos
            self.set(UC_X86_REG_AX, pos & 0xFFFF)
            self.set(UC_X86_REG_DX, pos >> 16)
        elif ah == 0x44:
            if al == 0:
                self.set(UC_X86_REG_DX, 0x80 if bx < 5 else 0)
            else:
                self.set(UC_X86_REG_AX, 0)
        elif ah == 0x4B and al == 3:                   # load overlay: es:bx -> (segment, reloc)
            name = self.cstr(ds, dx)
            path = self._path(name)
            if not path:
                self.carry(True)
                self.set(UC_X86_REG_AX, 2)
                return
            seg, reloc = self.rw(es * 16 + bx), self.rw(es * 16 + bx + 2)
            self.drivers[name.upper().split('\\')[-1]] = seg
            data = open(path, 'rb').read()
            hdr = struct.unpack('<14H', data[:28])
            hs = hdr[4] * 16
            size = (hdr[2] - 1) * 512 + (hdr[1] or 512) if hdr[2] else len(data)
            self.uc.mem_write(seg * 16, data[hs:size])
            self.uc.ctl_remove_cache(seg * 16, seg * 16 + size - hs)
            for i in range(hdr[3]):
                off, rseg = struct.unpack_from('<HH', data, hdr[12] + 4 * i)
                a = (seg + rseg) * 16 + off
                self.ww(a, self.rw(a) + reloc)
        elif ah == 0x4C:
            raise Halt()
        elif ah == 0x09:                               # print $-string
            text = self.rd(ds * 16 + dx, 400).split(b'$')[0]
            sys.stderr.write(text.decode('latin1'))
        elif ah in (0x02, 0x06) and not (ah == 0x06 and (dx & 0xFF) == 0xFF):
            sys.stderr.write(chr(dx & 0xFF))
        elif ah in (0x01, 0x07, 0x08) or ah == 0x06:   # keyboard input
            if getattr(self, 'pending_scan', None) is not None:
                # an extended key's second byte (DOS returns 0, then the scan code)
                self.set(UC_X86_REG_AX, (ah << 8) | self.pending_scan)
                self.pending_scan = None
                return
            if not self.keys and self._wait():
                return
            k = self.keys.pop(0) if self.keys else 0x1C0D
            self.poll_since = None
            if k & 0xFF == 0:
                self.pending_scan = k >> 8
            self.set(UC_X86_REG_AX, (ah << 8) | (k & 0xFF))
        elif ah == 0x2C:
            self.set(UC_X86_REG_CX, 0x0C00)
            self.set(UC_X86_REG_DX, 0x0000)
        elif ah == 0x2A:
            self.set(UC_X86_REG_CX, 1991)
            self.set(UC_X86_REG_DX, 0x0A01)
        elif ah == 0x19:
            self.set(UC_X86_REG_AX, 2)                 # drive C:
        elif ah == 0x0E:
            self.set(UC_X86_REG_AX, 3)
        elif ah == 0x47:
            self.uc.mem_write(ds * 16 + self.reg(UC_X86_REG_SI), b'\0')
        elif ah == 0x1A:
            self.dta = (ds, dx)
        elif ah in (0x11, 0x12):                       # FCB find (the disk check)
            fcb = self.rd(ds * 16 + dx, 40)
            if fcb[0] == 0xFF:                         # extended FCB
                fcb = fcb[7:]
            pat_name, pat_ext = fcb[1:9].decode('latin1'), fcb[9:12].decode('latin1')

            def match(p, s):
                return all(a == '?' or a == b for a, b in zip(p, s.ljust(len(p))))
            if ah == 0x11:
                names = sorted(os.listdir(self.data_dir))
                self.fcb_found = []
                for f in names:
                    b, _, e = f.upper().partition('.')
                    if match(pat_name, b[:8]) and match(pat_ext, e[:3]):
                        self.fcb_found.append((b[:8].ljust(8), e[:3].ljust(3)))
            if getattr(self, 'fcb_found', None):
                b, e = self.fcb_found.pop(0)
                dta_seg, dta_off = getattr(self, 'dta', (PSP_SEG, 0x80))
                self.uc.mem_write(dta_seg * 16 + dta_off, bytes([3]) + (b + e).encode() + bytes(25))
                self.set(UC_X86_REG_AX, ah << 8)
            else:
                self.set(UC_X86_REG_AX, (ah << 8) | 0xFF)
        elif ah in (0x4E, 0x4F):                       # find first / next (DTA format)
            import fnmatch
            if ah == 0x4E:
                pat = self.cstr(ds, dx).replace('\\', '/').split('/')[-1].upper()
                seen, found = set(), []
                for d in (self.scratch, self.data_dir):
                    for f in sorted(os.listdir(d)):
                        if fnmatch.fnmatch(f.upper(), pat) and f.upper() not in seen:
                            seen.add(f.upper())
                            found.append(os.path.join(d, f))
                self.find_list = found
            if not getattr(self, 'find_list', None):
                self.carry(True)
                self.set(UC_X86_REG_AX, 18)
                return
            path = self.find_list.pop(0)
            dseg, doff = getattr(self, 'dta', (PSP_SEG, 0x80))
            name = os.path.basename(path).upper().encode()[:12]
            rec = bytes(21) + bytes([0x20]) + struct.pack('<HHI', 0, 0x1721, os.path.getsize(path)) + name + b'\0'
            self.uc.mem_write(dseg * 16 + doff, rec.ljust(43, b'\0'))
            self.set(UC_X86_REG_AX, 0)
        elif ah == 0x43:
            self.set(UC_X86_REG_CX, 0)
        elif ah == 0x33:
            self.set(UC_X86_REG_DX, 0)
        elif ah == 0x0B:                               # key available?
            if not self.keys and getattr(self, 'pending_scan', None) is None:
                if self._polled():
                    return
            self.set(UC_X86_REG_AX, 0x0BFF if self.keys or getattr(self, 'pending_scan', None) is not None
                     else 0x0B00)
        elif ah == 0x29:
            self.set(UC_X86_REG_AX, 0)
        else:
            raise RuntimeError('unhandled DOS ah=%02x al=%02x at %04x:%04x' % (
                ah, al, self.reg(UC_X86_REG_CS), self.reg(UC_X86_REG_IP)))

    # ---- snapshots ---------------------------------------------------------------------
    _REGS = ('EAX', 'EBX', 'ECX', 'EDX', 'ESI', 'EDI', 'EBP', 'ESP', 'EIP', 'EFLAGS',
             'CS', 'DS', 'ES', 'SS', 'FS', 'GS')

    def snapshot(self):
        """The whole machine as a picklable value (memory, registers, devices, files)."""
        import copy
        state = {k: copy.deepcopy(v) for k, v in self.__dict__.items() if k not in ('uc', 'on_wait')}
        regs = {r: self.uc.reg_read(globals()['UC_X86_REG_' + r]) for r in self._REGS}
        return {'mem': self.rd(0, 0x110000), 'regs': regs, 'state': state}

    @classmethod
    def from_snapshot(cls, snap):
        import copy
        m = cls(snap['state']['data_dir'], snap['state']['scratch'])
        m.__dict__.update(copy.deepcopy(snap['state']))
        m.uc.mem_write(0, snap['mem'])
        for r, v in snap['regs'].items():
            m.uc.reg_write(globals()['UC_X86_REG_' + r], v)
        return m

    def run_until_wait(self, max_insns=4 * 10 ** 9):
        """Run until the program waits for input; False if it ran out of instructions or quit."""
        self.on_wait = None
        return self.run(max_insns) == 'wait'

    # ---- the game's state ------------------------------------------------------------
    LOAD = PSP_SEG + 0x10

    def save_bytes(self):
        """The saved game as the original would write it now (src/save.c's layout)."""
        from sve_layout import SVE_LAYOUT
        dgroup = (self.LOAD + 0x2A1B) * 16
        out = bytearray()
        for seg, addr, size in SVE_LAYOUT:
            if seg == 'ds':
                if addr.startswith('WORD PTR ds:'):
                    a = dgroup + self.rw(dgroup + int(addr[12:], 16))
                else:
                    a = dgroup + int(addr, 16)
            else:
                a = (self.LOAD + int(seg, 16)) * 16 + int(addr, 16)
            out += self.rd(a, size)
        return bytes(out)

    def page(self, n):
        """Graphics page n of the driver (2 is the map)."""
        seg = self.drivers.get('MGRAPHIC.EXE')
        if seg is None:
            return None
        pseg = self.rw(seg * 16 + 0x1970 + 2 * n)
        return self.rd(pseg * 16, 64000)

    def rng_state(self):
        dgroup = (self.LOAD + 0x2A1B) * 16
        return self.rw(dgroup + 0x5BDA) | self.rw(dgroup + 0x5BDC) << 16

    def set_rng(self, v):
        dgroup = (self.LOAD + 0x2A1B) * 16
        self.ww(dgroup + 0x5BDA, v)
        self.ww(dgroup + 0x5BDC, v >> 16)

    # ---- the screen --------------------------------------------------------------------
    def screen(self):
        """(pixels 320x200, palette 256x3 8-bit) of the mode 13h screen."""
        pix = self.rd(0xA0000, 64000)
        pal = bytes(min(255, v * 255 // 63) for v in self.dac)
        return pix, pal

    def save_png(self, path):
        from PIL import Image
        pix, pal = self.screen()
        im = Image.frombytes('P', (320, 200), pix)
        im.putpalette(pal)
        im.save(path)


# Script tokens -> BIOS keyboard codes (scan << 8 | ascii)
_LETTER_SCANS = dict(zip('qwertyuiop', range(0x10, 0x1A)))
_LETTER_SCANS.update(zip('asdfghjkl', range(0x1E, 0x27)))
_LETTER_SCANS.update(zip('zxcvbnm', range(0x2C, 0x33)))
KEYS = {'ENTER': 0x1C0D, 'ESC': 0x011B, 'SPACE': 0x3920, 'BKSP': 0x0E08, 'TAB': 0x0F09,
        'UP': 0x4800, 'DOWN': 0x5000, 'LEFT': 0x4B00, 'RIGHT': 0x4D00,
        'HOME': 0x4700, 'END': 0x4F00, 'PGUP': 0x4900, 'PGDN': 0x5100}
KEYS.update({'KP%d' % d: (sc << 8) | (0x30 + d) for d, sc in
             ((7, 0x47), (8, 0x48), (9, 0x49), (4, 0x4B), (5, 0x4C), (6, 0x4D), (1, 0x4F), (2, 0x50),
              (3, 0x51), (0, 0x52))})
KEYS.update({'F%d' % n: (0x3A + n) << 8 for n in range(1, 11)})


def key_code(tok):
    if tok in KEYS:
        return KEYS[tok]
    c = tok[0]
    if c.lower() in _LETTER_SCANS:
        return _LETTER_SCANS[c.lower()] << 8 | ord(c)
    if c.isdigit():
        return (0x0B if c == '0' else 1 + int(c)) << 8 | ord(c)
    return ord(c)


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--shots', default=os.path.join(ROOT, 'build', 'dos_shots'))
    ap.add_argument('--keys', default='')
    ap.add_argument('--max', type=int, default=200_000_000)
    ap.add_argument('--args', default='')
    a = ap.parse_args()
    os.makedirs(a.shots, exist_ok=True)
    m = DosMachine(verbose=True)
    m.load_exe('CIV.EXE', a.args)
    script = a.keys.split()
    n = [0]

    def on_wait(m):
        n[0] += 1
        if m.video_mode == 0x13:
            m.save_png(os.path.join(a.shots, 'wait%03d.png' % n[0]))
        else:
            print(m.text_screen(), file=sys.stderr)
        print('wait %d at %d insns' % (n[0], m.insns), file=sys.stderr)
        if script:
            k = script.pop(0)
            m.keys.append(key_code(k))
        else:
            raise Halt()
    m.on_wait = on_wait
    try:
        m.run(a.max)
    except Halt:
        pass
    print('stopped at %04x:%04x after %d instructions' % (m.reg(UC_X86_REG_CS), m.reg(UC_X86_REG_IP), m.insns),
          file=sys.stderr)


if __name__ == '__main__':
    main()
