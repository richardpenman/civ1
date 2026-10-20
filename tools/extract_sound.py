"""Record the sounds of the PC-speaker driver ISOUND.CVL and emit them as C data.

    python3 -I tools/extract_sound.py ~/Dos/Civ1/ISOUND.CVL src/sdl/sound_data.c

The game loads a sound driver (xSOUND.CVL) and calls it through a jump table: entry 1
starts sound n (0..44), entry 3 is the sequencer, run every 1/60 s from the game's timer
interrupt (it returns >0 when it wants the fast timer, <0 for the slow one), and entry 4
runs at every timer interrupt while the fast timer (300 Hz) is on, for vibrato and noise.
The driver drives the speaker through PIT channel 2 (ports 0x42/0x43) and port 0x61.
This tool runs the original driver in Unicorn exactly so and records, every 1/300 s, the
PIT divisor sounding on the speaker (0 = silent).  The SDL front end plays these back.
"""
import os, struct, sys
_tc = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), '.toolchain/root/usr')
sys.path.insert(0, os.path.join(_tc, 'lib/python3/dist-packages'))
if os.path.exists(os.path.join(_tc, 'lib/x86_64-linux-gnu/libunicorn.so.2')):
    import ctypes
    ctypes.CDLL(os.path.join(_tc, 'lib/x86_64-linux-gnu/libunicorn.so.2'), mode=ctypes.RTLD_GLOBAL)
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_DX, UC_X86_REG_CX, UC_X86_REG_CS, UC_X86_REG_IP,
                               UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_BP,
                               UC_X86_INS_IN, UC_X86_INS_OUT)

SEG = 0x1000          # where the driver is loaded
STACK = 0x8000        # stack segment
RET = 0x9000          # sentinel return segment (offset 0)
NSOUNDS = 45
FAST = 300            # samples per second
MAX_SECONDS = 120


class Driver:
    def __init__(self, path):
        d = open(path, 'rb').read()
        hdr = struct.unpack_from('<H', d, 8)[0] * 16
        nrel, reltab = struct.unpack_from('<H', d, 6)[0], struct.unpack_from('<H', d, 0x18)[0]
        image = bytearray(d[hdr:])
        for i in range(nrel):
            off, seg = struct.unpack_from('<HH', d, reltab + 4 * i)
            p = seg * 16 + off
            struct.pack_into('<H', image, p, (struct.unpack_from('<H', image, p)[0] + SEG) & 0xFFFF)
        self.image = bytes(image)
        self.entries = [struct.unpack_from('<H', self.image, 0x32 + 2 * i)[0]
                        for i in range(struct.unpack_from('<H', self.image, 0x30)[0])]
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x100000)
        self.uc.mem_write(SEG * 16, self.image)
        self.uc.mem_write(RET * 16, b'\xf4')
        self.uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_INTR, self._intr)
        self.port61 = 0
        self.divisor = 0
        self.latch_hi = False
        self.lo = 0

    def _in(self, uc, port, size, data):
        return self.port61 if port == 0x61 else 0

    def _out(self, uc, port, size, value, data):
        value &= 0xFF
        if port == 0x61:
            self.port61 = value
        elif port == 0x43:
            self.latch_hi = False
        elif port == 0x42:
            if not self.latch_hi:
                self.lo = value
                self.latch_hi = True
            else:
                self.divisor = self.lo | value << 8
                self.latch_hi = False

    def _intr(self, uc, intno, data):
        uc.reg_write(UC_X86_REG_CX, 0)
        uc.reg_write(UC_X86_REG_DX, 0)
        uc.reg_write(UC_X86_REG_AX, 0)

    def call(self, entry, *args):
        uc = self.uc
        sp = 0xFFF0
        for a in reversed(args):
            sp -= 2
            uc.mem_write(STACK * 16 + sp, struct.pack('<H', a & 0xFFFF))
        sp -= 4
        uc.mem_write(STACK * 16 + sp, struct.pack('<HH', 0, RET))
        for reg, v in ((UC_X86_REG_SS, STACK), (UC_X86_REG_SP, sp), (UC_X86_REG_DS, SEG),
                       (UC_X86_REG_ES, SEG), (UC_X86_REG_BP, 0), (UC_X86_REG_CS, SEG),
                       (UC_X86_REG_IP, self.entries[entry])):
            uc.reg_write(reg, v)
        uc.emu_start(SEG * 16 + self.entries[entry], RET * 16, count=200000)
        ax = uc.reg_read(UC_X86_REG_AX)
        return ax - 0x10000 if ax & 0x8000 else ax

    def sounding(self):
        return self.divisor if (self.port61 & 3) == 3 else 0


def record(path, n):
    """One sound: the speaker's divisor every 1/300 s, until it has been quiet for 2 s."""
    drv = Driver(path)
    drv.call(0)
    drv.call(1, n, 0)
    out, fast, quiet, heard = [], False, 0, False
    for base in range(MAX_SECONDS * 60):
        r = drv.call(3)
        if r > 0:
            fast = True
        elif r < 0:
            fast = False
        for k in range(5):
            if fast:
                drv.call(4)
            s = drv.sounding()
            out.append(s)
            if s:
                heard, quiet = True, 0
            else:
                quiet += 1
        if heard and quiet > 2 * FAST:
            break
        if not heard and base > 2 * 60:
            break
    while out and out[-1] == 0:
        out.pop()
    return out


def rle(samples):
    runs = []
    for s in samples:
        if runs and runs[-1][0] == s and runs[-1][1] < 65535:
            runs[-1][1] += 1
        else:
            runs.append([s, 1])
    return runs


def main():
    src, dst = sys.argv[1], sys.argv[2]
    sounds = [rle(record(src, n)) for n in range(NSOUNDS)]
    o = ['/* Generated by tools/extract_sound.py from %s -- do not edit. */' % src.split('/')[-1],
         '/* Each sound: runs of (PIT divisor sounding on the speaker, 0 = silence; length in',
         ' * 1/300 s), ending with a zero length. */',
         '#include "sound_data.h"', '']
    for n, runs in enumerate(sounds):
        body = ', '.join('{ %d, %d }' % (d, c) for d, c in runs)
        o.append('static const SoundRun sound_%d[] = { %s%s{ 0, 0 } };' % (n, body, ', ' if body else ''))
    o.append('')
    o.append('const SoundRun *const speaker_sounds[NUM_SOUNDS] = { %s };' %
             ', '.join('sound_%d' % n for n in range(NSOUNDS)))
    open(dst, 'w').write('\n'.join(o) + '\n')
    for n, runs in enumerate(sounds):
        print(n, '%.1fs' % (sum(c for d, c in runs) / FAST), len(runs), 'runs', file=sys.stderr)


if __name__ == '__main__':
    main()
