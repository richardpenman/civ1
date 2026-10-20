"""Make a test scenario from a saved game:

    python3 -I tools/mksave.py IN.SVE IN.MAP OUT.SVE OUT.MAP field=value ... [unitlayer:x,y,civ ...]

Fields are named as save_field_name() names them (units[7][22].type=3); a value of a word
field sets its low byte.  unitlayer:x,y,civ marks a square as holding civ's units in the
map's unit layer (where the game looks for units first)."""
import ctypes, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lib = ctypes.CDLL(os.path.join(ROOT, 'libciv.so'))
lib.capi_map_pixels.restype = ctypes.POINTER(ctypes.c_uint8)
if lib.capi_load(sys.argv[1].encode(), sys.argv[2].encode()):
    sys.exit('cannot load %s' % sys.argv[1])
buf = (ctypes.c_uint8 * 37856)()
lib.capi_save_bytes(buf)
name = ctypes.create_string_buffer(64)
offsets = {}
for i in range(37856):
    lib.save_field_name(ctypes.c_long(i), name, ctypes.c_size_t(64))
    offsets.setdefault(name.value.decode(), i)
pix = lib.capi_map_pixels()
for arg in sys.argv[5:]:
    if arg.startswith('unitlayer:'):
        x, y, civ = (int(v) for v in arg[10:].split(','))
        pix[y * 320 + x + 160] = (pix[y * 320 + x + 160] & ~0xF) | 8 | civ
        continue
    k, v = arg.split('=')
    buf[offsets[k]] = int(v, 0) & 0xFF
lib.capi_load_bytes(buf)
if lib.capi_save(sys.argv[3].encode(), sys.argv[4].encode()):
    sys.exit('cannot write %s' % sys.argv[3])
