"""ctypes binding to libciv.so (the C port)."""
import ctypes, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVE_SIZE = 37856


class Port:
    def __init__(self):
        self.lib = ctypes.CDLL(os.path.join(ROOT, 'libciv.so'))
        self.lib.capi_map_pixels.restype = ctypes.c_void_p

    def load(self, sve, map_):
        assert self.lib.capi_load(sve.encode(), map_.encode()) == 0

    def save_bytes(self):
        buf = ctypes.create_string_buffer(SVE_SIZE)
        self.lib.capi_save_bytes(buf)
        return buf.raw

    def load_bytes(self, data):
        self.lib.capi_load_bytes(ctypes.c_char_p(data))

    def var(self, name):
        """A C int global of the port, e.g. port.var('cur_player').value = 3."""
        return ctypes.c_int.in_dll(self.lib, name)

    def var_ptr(self, name):
        return ctypes.c_void_p.in_dll(self.lib, name)

    def map_page(self):
        return ctypes.string_at(self.lib.capi_map_pixels(), 320 * 200)

    def ai_goals(self):
        """x, y, type, prio arrays as bytes (8 civs x 32)."""
        g = ctypes.string_at(ctypes.addressof(ctypes.c_byte.in_dll(self.lib, 'ai_goals')), 8 * 128)
        return b''.join(g[c * 128 + k * 32:c * 128 + k * 32 + 32] for k in range(4) for c in range(8))

    def __getattr__(self, name):
        f = getattr(self.lib, name)
        f.restype = ctypes.c_int
        return f
