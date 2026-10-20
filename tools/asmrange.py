"""Print disassembly lines of a file between two addresses: asmrange.py ovl_06 008f 010c"""
import re, sys, os
path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'build/asm', sys.argv[1] + '.asm')
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
for line in open(path):
    m = re.match(r'\s+([0-9a-f]{4})\s', line)
    if m and lo <= int(m.group(1), 16) < hi:
        parts = line.rstrip().split(None, 1)
        print(line.rstrip()[:90])
