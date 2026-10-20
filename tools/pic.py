"""Decode Civilization .PIC/.PAL files (chunked: E0 EGA map, M0 VGA palette, X0/X1 LZW+RLE image)."""
import struct, sys

def read_chunks(data):
    p = 0; chunks = {}
    while p + 4 <= len(data):
        tag = data[p:p + 2].decode('latin1')
        if tag[1] != '0' and tag[1] != '1':
            break
        n, = struct.unpack('<H', data[p + 2:p + 4])
        if tag[0] == 'X':
            chunks[tag] = data[p + 4:]   # image runs to end of file
            break
        chunks[tag] = data[p + 4:p + 4 + n]
        p += 4 + n
    return chunks

def palette(m0):
    first, last = m0[0], m0[1]
    pal = [(0, 0, 0)] * 256
    for i in range(first, last + 1):
        r, g, b = m0[2 + 3 * (i - first):5 + 3 * (i - first)]
        pal[i] = (r * 255 // 63, g * 255 // 63, b * 255 // 63)
    return pal

def lzw(src, maxbits):
    """Mirror of the game's decoder (root seg 0x1219-0x13b3): a 0x800-entry prefix/char table,
    an entry is added for every code (even right after a reset), and the old code survives resets."""
    maxbits = min(maxbits, 11)
    prefix = [0xFFFF] * 0x800
    char = [0] * 0x800
    for i in range(256):
        char[i] = i
    out = bytearray(); bitbuf = 0; nbits = 0; p = 0
    width, mask, nxt = 9, 0x1FF, 0x100
    old = 0; oldchar = 0
    while True:
        while nbits < width and p < len(src):
            bitbuf |= src[p] << nbits; nbits += 8; p += 1
        if nbits < width:
            break
        code = bitbuf & mask; bitbuf >>= width; nbits -= width
        cur = code
        stack = []
        if code >= nxt:
            stack.append(oldchar)
            code = old
        c = code
        while prefix[c] != 0xFFFF:
            stack.append(char[c]); c = prefix[c]
        first = char[c]
        stack.append(first)
        oldchar = first
        out += bytes(reversed(stack))
        char[nxt] = first; prefix[nxt] = old
        nxt += 1
        if nxt > mask:
            width += 1; mask = mask * 2 + 1
            if width > maxbits:
                prefix = [0xFFFF] * 0x800
                for i in range(256):
                    char[i] = i
                width, mask, nxt = 9, 0x1FF, 0x100
        old = cur
    return out

def unrle(src):
    out = bytearray(); i = 0; last = 0
    while i < len(src):
        b = src[i]; i += 1
        if b == 0x90 and i < len(src):
            n = src[i]; i += 1
            if n == 0:
                out.append(0x90); last = 0x90
            else:
                out += bytes([last]) * (n - 1)
        else:
            out.append(b); last = b
    return out

def image(chunk, four_bit):
    w, h, maxbits = struct.unpack("<HHB", chunk[:5])
    w += w & 1                     # rows are kept an even number of pixels wide (TORCH.PIC)
    pix = unrle(lzw(chunk[5:], maxbits))
    if four_bit:
        pix = bytearray(v for b in pix for v in (b & 15, b >> 4))
    return w, h, bytes(pix[:w * h])

def load(path):
    c = read_chunks(open(path, 'rb').read())
    pal = palette(c['M0']) if 'M0' in c else None
    img = None
    if 'X0' in c:
        img = image(c['X0'], False)
    elif 'X1' in c:
        img = image(c['X1'], True)
    return c, pal, img

if __name__ == '__main__':
    from PIL import Image
    src, dst = sys.argv[1], sys.argv[2]
    c, pal, img = load(src)
    if pal is None and len(sys.argv) > 3:
        pal = load(sys.argv[3])[1]
    w, h, pix = img
    im = Image.frombytes('P', (w, h), pix.ljust(w * h, b'\0'))
    im.putpalette([v for rgb in (pal or [(i, i, i) for i in range(256)]) for v in rgb])
    im.save(dst)
    print(src, sorted(c), w, h, len(pix))
