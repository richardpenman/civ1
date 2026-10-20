"""Unpack a Microsoft EXEPACK-compressed DOS executable into a plain MZ file."""
import struct, sys

def unpack(data):
    (last, pages, nrel, hdrp, minalloc, maxalloc, ss, sp, csum, ip, cs) = struct.unpack('<HHHHHHHHHHH', data[2:24])
    img_size = (pages - 1) * 512 + (last or 512)
    hdr = hdrp * 16
    image = data[hdr:img_size]
    stub = cs * 16
    real_ip, real_cs, _mem, pack_size, real_sp, real_ss, dest_len, sig = struct.unpack('<HHHHHHHH', image[stub:stub + 16])
    assert sig == 0x4252, 'not EXEPACK'
    packed = bytearray(image[:stub])
    out = bytearray(dest_len * 16)
    out[:len(packed)] = packed
    src = len(packed) - 1
    while packed[src] == 0xFF:
        src -= 1
    dst = len(out) - 1
    while True:
        cmd = packed[src]; src -= 1
        count = packed[src] << 8 | packed[src - 1]; src -= 2
        if cmd & 0xFE == 0xB0:
            b = packed[src]; src -= 1
            for _ in range(count):
                out[dst] = b; dst -= 1
        elif cmd & 0xFE == 0xB2:
            for _ in range(count):
                out[dst] = packed[src]; dst -= 1; src -= 1
        else:
            raise ValueError('bad cmd %02x' % cmd)
        if cmd & 1:
            break
    # relocation table follows the stub's error message
    msg = image.find(b'Packed file is corrupt', stub)
    p = msg + len(b'Packed file is corrupt')
    relocs = []
    for seg in range(16):
        n, = struct.unpack('<H', image[p:p + 2]); p += 2
        for _ in range(n):
            off, = struct.unpack('<H', image[p:p + 2]); p += 2
            relocs.append((off, seg * 0x1000))
    hsize = (28 + 4 * len(relocs) + 511) // 512 * 512
    total = hsize + len(out)
    h = struct.pack('<2sHHHHHHHHHHHHH', b'MZ', total % 512, (total + 511) // 512, len(relocs), hsize // 16,
                    minalloc, maxalloc, real_ss, real_sp, 0, real_ip, real_cs, 28, 0)
    h += b''.join(struct.pack('<HH', o, s) for o, s in relocs)
    h = h.ljust(hsize, b'\0')
    return h + bytes(out) + data[img_size:], relocs

if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    res, relocs = unpack(data)
    open(sys.argv[2], 'wb').write(res)
    print('relocs', len(relocs), 'size', len(res))
