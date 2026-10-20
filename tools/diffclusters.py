"""Group the differing pixels of a lockstep step into boxes: python3 -I tools/diffclusters.py N"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lockstep as L

n = int(sys.argv[1])
d = sys.argv[2] if len(sys.argv) > 2 else os.path.join(L.ROOT, 'build', 'lockstep')
a, b = L.load_step(os.path.join(d, 'orig'), n), L.load_step(os.path.join(d, 'port'), n)
diff = set(i for i in range(64000) if a['rgb'][a['pix'][i]] != b['rgb'][b['pix'][i]])
seen, boxes = set(), []
for i in sorted(diff):
    if i in seen:
        continue
    stack, pts = [i], []
    seen.add(i)
    while stack:
        j = stack.pop()
        pts.append(j)
        x, y = j % 320, j // 320
        for dx in range(-3, 4):
            for dy in range(-3, 4):
                k = (y + dy) * 320 + x + dx
                if 0 <= x + dx < 320 and 0 <= y + dy < 200 and k in diff and k not in seen:
                    seen.add(k)
                    stack.append(k)
    xs, ys = [p % 320 for p in pts], [p // 320 for p in pts]
    boxes.append((len(pts), min(xs), min(ys), max(xs), max(ys)))
for c in sorted(boxes, key=lambda b: (b[2], b[1])):
    print('%4d px  x %3d-%3d  y %3d-%3d' % (c[0], c[1], c[3], c[2], c[4]))
