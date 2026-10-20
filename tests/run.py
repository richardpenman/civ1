"""Differential tests: the C port (libciv.so) against the original EXE run in an emulator.

    python3 -I tests/run.py [test-name-substring]
"""
import ctypes, glob, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from oracle import Emu, DGROUP, ORIG  # noqa: E402
from port import Port  # noqa: E402

SEEDS = [int(v) for v in os.environ.get('CIV_SEEDS', '99').split(',')]
SAVES = sorted(glob.glob(os.path.join(ORIG, 'CIVIL*.SVE')))
TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


def saves():
    for s in SAVES:
        yield s, s[:-4] + '.MAP'


@test
def tile_yield(emu, port):
    """f_0d07_68f0 for every square of every city, all three kinds."""
    n = 0
    for sve, mp in saves():
        emu.reset()
        emu.load_save(sve, mp)
        port.load(sve, mp)
        data = open(sve, 'rb').read()
        for city in range(128):
            rec = data[0x1508 + 28 * city:0x1508 + 28 * city + 28]
            if rec[6] == 0xFF:
                continue
            emu.ww(emu.ds(0x64c2), city)
            emu.ww(emu.ds(0x64ca), rec[11])
            for sq in range(21):
                x = rec[4] + [0, 1, 0, -1, 1, 1, -1, -1, 0, 2, 0, -2, -1, 1, 2, 2, 1, -1, -2, -2, 0][sq]
                y = rec[5] + [-1, 0, 1, 0, -1, 1, 1, -1, -2, 0, 2, 0, -2, -2, -1, 1, 2, 2, 1, -1, 0][sq]
                for kind in range(3):
                    want = emu.call('f_0d07_68f0', x, y, kind)
                    got = port.tile_yield(city, x, y, kind)
                    assert got == want, '%s city %d (%d,%d) kind %d: port %d, original %d' % (
                        os.path.basename(sve), city, x, y, kind, got, want)
                    n += 1
    return '%d yields' % n


# Original functions not yet ported: stubbed in the emulator, no-ops in the port.
TEMP_STUBS = []

# Where the original reads uninitialised memory the port defines a value; the oracle is
# patched to read the same thing.  (segment, offset, original bytes, patched bytes)
UNDEFINED_PATCHES = [
    # ai_unit_command, transports: city[l12].size with l12 never set -> the nearest city (l52)
    (0x15da, 0x186c, bytes.fromhex('f76ef4'), bytes.fromhex('f76ecc')),
    # barbarian uprisings: enemy_in_zone(l2, ...) with l2 never set (in play it holds a stack
    # address, i.e. no civilization) -> push -1
    (0x0237, 0x0b82, bytes.fromhex('ff76fe'), bytes.fromhex('6aff90')),
]


def new_emu():
    emu = Emu(os.path.join(ORIG, 'CIV.EXE'))
    for name, result in TEMP_STUBS:
        emu.stub(name, result)
    for seg, off, old, new in UNDEFINED_PATCHES:
        emu.patch(seg, off, old, new)
    return emu


def emu_ai_goals(emu):
    return b''.join(emu.rd(emu.ds(base), 256) for base in (0xf4b8, 0xf6b8, 0xf1e2, 0xc1de))


def compare_state(emu, port, what):
    ra = emu.rw(emu.ds(0x5bda)) | emu.rw(emu.ds(0x5bdc)) << 16
    rb = port.lib.capi_rand_state() & 0xFFFFFFFF
    if ra != rb:
        raise AssertionError('%s: random number state differs (original %08x, port %08x)' % (what, ra, rb))
    a, b = emu.save_bytes(), port.save_bytes()
    if a != b:
        i = next(k for k in range(len(a)) if a[k] != b[k])
        raise AssertionError('%s: save differs at 0x%04x (original %02x, port %02x)' % (what, i, a[i], b[i]))
    pa, pb = bytes(emu.page[2]), port.map_page()
    if pa != pb:
        i = next(k for k in range(len(pa)) if pa[k] != pb[k])
        raise AssertionError('%s: map page differs at (%d,%d) (original %d, port %d)' % (
            what, i % 320, i // 320, pa[i], pb[i]))
    fa = bytes(emu.uc.mem_read(emu.lin(0x2786, 0x1498), 260))
    fb = ctypes.string_at(ctypes.addressof(ctypes.c_ubyte.in_dll(port.lib, 'ai_block_flags')), 260)
    if fa != fb:
        i = next(k for k in range(260) if fa[k] != fb[k])
        raise AssertionError('%s: AI block flags differ at %d (original %d, port %d)' % (what, i, fa[i], fb[i]))
    ga, gb = emu_ai_goals(emu), port.ai_goals()
    if ga != gb:
        i = next(k for k in range(len(ga)) if ga[k] != gb[k])
        raise AssertionError('%s: AI goals differ (array %d civ %d slot %d: original %d, port %d)' % (
            what, i // 256, i % 256 // 32, i % 32, ga[i], gb[i]))


def start(emu, port, sve, mp, player=0):
    emu.reset()
    emu.load_save(sve, mp)
    port.load(sve, mp)
    emu.ww(emu.ds(0x6b10), player)
    port.var('cur_player').value = player
    emu.ww(emu.ds(0x3434), 0xFFFD)     # as the original's loader leaves it (port: load_marker)
    seed(emu, port, 1)


def seed(emu, port, value):
    """Same MSC rand() state on both sides."""
    emu.ww(emu.ds(0x5bda), value & 0xFFFF)
    emu.ww(emu.ds(0x5bdc), value >> 16)
    port.capi_srand(value)


def city_ids(sve):
    data = open(sve, 'rb').read()
    return [c for c in range(128) if data[0x1508 + 28 * c + 6] != 0xFF]


@test
def city_turn(emu, port):
    """city_process(city, 0): one city's turn, for every city of every save."""
    import ctypes
    n = 0
    for sve, mp in saves():
        for city in city_ids(sve):
            start(emu, port, sve, mp)
            seed(emu, port, 1000 + city)
            want = emu.call('city_process', city, 0, 0, si=city * 28)   # SI as the turn loop leaves it
            got = port.city_process(city, ctypes.c_long(0))
            what = '%s city %d' % (os.path.basename(sve), city)
            compare_state(emu, port, what)
            for name, addr in (('ai_support_score', 0xf2e2), ('ai_unhappy_score', 0xea62)):
                a, b = emu.g(addr), port.var(name).value
                assert a == b, '%s: %s original %d port %d' % (what, name, a, b)
            destroyed = emu.save_bytes()[0x1508 + 28 * city + 6] == 0xFF
            # (a destroyed city's return value is whatever AX held; nobody uses it)
            assert destroyed or got == want, '%s: returned %d, original %d' % (what, got, want)
            n += 1
    return '%d cities' % n


@test
def units_create_delete(emu, port):
    """create_unit / delete_unit at many places: stacks, visibility, contact, AI goals."""
    import random
    rng = random.Random(1)
    n = 0
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        alive = data[0x0c]
        for civ in range(1, 8):
            if not alive & (1 << civ):
                continue
            start(emu, port, sve, mp, civ)
            for k in range(12):
                x, y = rng.randrange(80), rng.randrange(2, 48)
                t = rng.randrange(28)
                want = emu.call('create_unit', civ, t, x, y)
                got = port.create_unit(civ, t, x, y)
                assert got == want, '%s civ %d create type %d at (%d,%d): port %d original %d' % (
                    os.path.basename(sve), civ, t, x, y, got, want)
                compare_state(emu, port, '%s civ %d create #%d' % (os.path.basename(sve), civ, k))
                if want >= 0 and k % 3 == 2:
                    emu.call('delete_unit', civ, want)
                    port.delete_unit(civ, want)
                    compare_state(emu, port, '%s civ %d delete #%d' % (os.path.basename(sve), civ, k))
                n += 1
    return '%d units' % n


@test
def civ_cities(emu, port):
    """civ_process_cities(civ): all of a civ's cities plus the AI's tax/science choice."""
    n = 0
    for sve, mp in saves():
        alive = open(sve, 'rb').read()[0x0c]
        for civ in range(8):
            if not alive & (1 << civ):
                continue
            start(emu, port, sve, mp)
            seed(emu, port, 77 + civ)
            emu.call('civ_process_cities', civ)
            port.civ_process_cities(civ)
            compare_state(emu, port, '%s civ %d' % (os.path.basename(sve), civ))
            n += 1
    return '%d civs' % n


class ScriptedAI:
    """Stands in for the AI's choice of orders (f_15da_0c76) on both sides."""
    KEYS = [1, 2, 3, 4, 5, 6, 7, 8] * 6 + [0, 0, 0, ord(' '), ord('b'), ord('f'), ord('i'), ord('m'),
                                           ord('r'), ord('s'), ord('p'), ord('P'), ord('h'), ord('u'),
                                           ord('w'), ord('D')]

    def __init__(self, seed):
        import random
        self.rng = random.Random(seed)
        self.log = []

    def __call__(self, civ, unit):
        k = self.rng.choice(self.KEYS)
        self.log.append((civ, unit, k))
        return k


@test
def ai_moves_scripted(emu, port):
    """civ_move_units for computer players, with scripted orders: movement, combat, cities..."""
    import ctypes
    HOOK = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_int)
    n = 0
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        alive, human = data[0x0c], data[2]
        for civ in range(8):
            if not alive & (1 << civ) or civ == human:
                continue
            for trial in range(2):
                seed_value = 1000 * civ + trial
                start(emu, port, sve, mp, civ)
                seed(emu, port, 4242 + seed_value)
                a = ScriptedAI(seed_value)
                emu.stub('f_15da_0c76', lambda e, args, a=a: a(e.arg(args, 0), e.arg(args, 1)))
                emu.call('civ_move_units', civ)
                b = ScriptedAI(seed_value)
                hook = HOOK(lambda c, u, b=b: b(c, u))
                port.var_ptr('ai_unit_command_hook').value = ctypes.cast(hook, ctypes.c_void_p).value
                port.civ_move_units(civ)
                port.var_ptr('ai_unit_command_hook').value = None
                what = '%s civ %d trial %d' % (os.path.basename(sve), civ, trial)
                if a.log != b.log:
                    k = next((i for i in range(min(len(a.log), len(b.log))) if a.log[i] != b.log[i]), None)
                    raise AssertionError('%s: order requests differ at #%s: original %s port %s' % (
                        what, k, a.log[k - 2:k + 2] if k else a.log[-3:], b.log[k - 2:k + 2] if k else b.log[-3:]))
                compare_state(emu, port, what)
                n += 1
    return '%d civ turns' % n



@test
def ai_moves(emu, port):
    """civ_move_units for computer players with their own orders (ai_unit_command)."""
    n = 0
    hung = []
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        alive, human = data[0x0c], data[2]
        for civ, base in ((c, b) for c in range(8) for b in SEEDS):
            if not alive & (1 << civ) or civ == human:
                continue
            start(emu, port, sve, mp, civ)
            seed(emu, port, base + civ)
            what = '%s civ %d seed %d' % (os.path.basename(sve), civ, base + civ)
            try:
                emu.call('civ_move_units', civ, timeout_s=60)
            except RuntimeError as e:
                if 'did not return' not in str(e):
                    raise AssertionError('%s: original: %s' % (what, e))
                hung.append(what)            # the original never finishes (a broken stack ring)
                continue
            port.civ_move_units(civ)
            compare_state(emu, port, what)
            n += 1
    return '%d civ turns%s' % (n, ' (original hangs: %s)' % ', '.join(hung) if hung else '')

@test
def ai_strategy(emu, port):
    """ai_strategy + ai_strategy2: continent policies, government, disbanding, unit goals."""
    n = 0
    for sve, mp in saves():
        alive = open(sve, 'rb').read()[0x0c]
        for civ in range(8):
            if not alive & (1 << civ):
                continue
            for turn_offset in range(0, 16, 5):
                start(emu, port, sve, mp, civ)
                seed(emu, port, 7 + civ)
                if turn_offset:              # other turns exercise the government and disband rules
                    set_turn(emu, port, emu.rw(emu.ds(0x90f2)) + turn_offset)
                emu.call('ai_strategy', civ)
                emu.call('ai_strategy2', civ)
                port.ai_strategy(civ)
                port.ai_reset_goals(civ)
                compare_state(emu, port, '%s civ %d turn+%d' % (os.path.basename(sve), civ, turn_offset))
                n += 1
    return '%d civ turns' % n



def set_turn(emu, port, turn):
    emu.ww(emu.ds(0x90f2), turn)
    data = bytearray(port.save_bytes())
    data[0:2] = turn.to_bytes(2, 'little')
    port.load_bytes(bytes(data))


@test
def ai_diplomacy(emu, port):
    """ai_diplomacy: anarchy ending, ceasefires, lapsing contacts, envoys."""
    n = 0
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        alive, turn = data[0x0c], int.from_bytes(data[0:2], 'little')
        for civ in range(8):
            if not alive & (1 << civ):
                continue
            for t in (turn, turn - turn % 16, turn - turn % 32 + civ * 4, turn - turn % 4 + 1):
                start(emu, port, sve, mp, civ)
                seed(emu, port, 11 + civ + t)
                set_turn(emu, port, t)
                emu.call('ai_diplomacy', civ)
                port.ai_diplomacy(civ)
                compare_state(emu, port, '%s civ %d turn %d' % (os.path.basename(sve), civ, t))
                n += 1
    return '%d cases' % n


@test
def end_of_turn(emu, port):
    """Barbarians, world events (AI wonders, warming), ranking, historians, per save and seed."""
    n = 0
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        turn = int.from_bytes(data[0:2], 'little')
        for k, t in enumerate((turn, turn - turn % 8 + 7, turn + 3)):
            for name, cname in (('f_0237_0982', 'barbarians_appear'), ('f_0237_1763', 'world_events'),
                                ('f_0237_0d9d', 'civ_ranking'), ('o_12_09d6', 'historian_report')):
                for sd in range(3):
                    start(emu, port, sve, mp)
                    seed(emu, port, 1234 + 17 * sd + k)
                    set_turn(emu, port, t)
                    emu.call(name)
                    getattr(port, cname)()
                    compare_state(emu, port, '%s %s turn %d seed %d' % (os.path.basename(sve), cname, t, sd))
                    n += 1
    return '%d calls' % n


@test
def disasters(emu, port):
    """o_20_053d for every city with several seeds."""
    n = 0
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        for city in range(128):
            if data[0x1508 + 28 * city + 6] == 0xFF:
                continue
            for sd in range(4):
                start(emu, port, sve, mp)
                seed(emu, port, 555 + city * 7 + sd)
                emu.call('o_20_053d', city)
                port.city_disaster(city)
                compare_state(emu, port, '%s city %d seed %d' % (os.path.basename(sve), city, sd))
                n += 1
    return '%d cases' % n


@test
def global_warming(emu, port):
    """o_07_1b82(n): terrain changes of the nth global warming."""
    n = 0
    for sve, mp in saves():
        for k in (1, 2, 4):
            start(emu, port, sve, mp)
            emu.call('o_07_1b82', k)
            port.global_warming(k)
            compare_state(emu, port, '%s warming %d' % (os.path.basename(sve), k))
            n += 1
    return '%d cases' % n


@test
def whole_turns(emu, port):
    """game_turn_loop for CIV_TURNS (default 1) turns in a row; the human's civilization sits out (its alive
    bit is cleared) since its moves come from the player."""
    n = 0
    hung = []
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        human = data[2]
        start(emu, port, sve, mp)
        seed(emu, port, 4321 + n)
        alive = int.from_bytes(data[0x0c:0x0e], 'little') & ~(1 << human)
        emu.ww(emu.ds(0x6b0a), alive)
        b = bytearray(port.save_bytes())
        b[0x0c:0x0e] = alive.to_bytes(2, 'little')
        port.load_bytes(bytes(b))
        for t in range(int(os.environ.get('CIV_TURNS', '1'))):
            what = '%s turn %d' % (os.path.basename(sve), t)
            try:
                emu.call('game_turn_loop', timeout_s=300)
            except RuntimeError as e:
                if 'did not return' not in str(e):
                    raise AssertionError('%s: original: %s' % (what, e))
                hung.append(what)            # the original never finishes (a broken stack ring)
                break
            port.game_turn()
            compare_state(emu, port, what)
            n += 1
    return '%d turns%s' % (n, ' (original hangs: %s)' % ', '.join(hung) if hung else '')


@test
def land_links(emu, port):
    """o_07_1142 on each saved map (block connectivity for land units)."""
    n = 0
    for sve, mp in saves():
        start(emu, port, sve, mp)
        emu.call('o_07_1142', 0, 0, timeout_s=600)
        port.build_land_links()
        compare_state(emu, port, os.path.basename(sve))
        n += 1
    return '%d maps' % n


@test
def world_generation(emu, port):
    """o_07_0012: new worlds for several parameter sets and seeds (the intro animation
    stubbed; the land links read the world being made, see NOTES.md)."""
    import ctypes
    from oracle import UC_HOOK_CODE, OVL_SEG
    sve, mp = next(saves())
    emu.gfx = True
    emu.stub('o_07_176e', 0)

    def copy_work_page(uc, addr, size, d):
        if emu.loaded_ovl == 7:
            emu.page[2][:] = emu.page[1]
    a = emu.lin(OVL_SEG, 0x0dd5)
    emu.uc.hook_add(UC_HOOK_CODE, copy_work_page, begin=a, end=a)
    emu.uc.ctl_remove_cache(0, 0x100000)
    world = (ctypes.c_int * 4).in_dll(port.lib, 'world')
    n = 0
    for params in ((1, 1, 1, 1), (0, 0, 0, 0), (2, 2, 2, 2), (0, 2, 1, 2), (2, 0, 2, 0)):
        for sd in range(2):
            start(emu, port, sve, mp)
            seed(emu, port, 777 + 31 * n)
            for k, v in enumerate(params):
                emu.ww(emu.ds(0x7e76 + 2 * k), v)
                world[k] = v
            emu.ww(emu.ds(0x6780), 0)
            port.var('land_mass').value = 0
            emu.call('o_07_0012', timeout_s=600)
            port.generate_world()
            what = 'params %s seed %d' % (params, sd)
            compare_state(emu, port, what)
            sa = bytes(emu.uc.mem_read(emu.ds(0x7eb8), 260))
            sb = ctypes.string_at(ctypes.addressof(ctypes.c_ubyte.in_dll(port.lib, 'sea_links')), 260)
            assert sa == sb, '%s: sea links differ at %d' % (what, next(i for i in range(260) if sa[i] != sb[i]))
            n += 1
    for sd in range(2):                          # the Earth
        start(emu, port, sve, mp)
        seed(emu, port, 99 + sd)
        emu.ww(emu.ds(0xe68a), 1)
        port.var('earth_map').value = 1
        emu.call('o_07_0012', timeout_s=600)
        port.generate_world()
        port.var('earth_map').value = 0
        compare_state(emu, port, 'earth seed %d' % sd)
        n += 1
    emu.gfx = False
    return '%d worlds' % n


@test
def new_games(emu, port):
    """A new world and o_05_0000 (settings, every civ's start, start balancing), menus
    answered with their first line."""
    import ctypes
    from oracle import UC_HOOK_CODE, OVL_SEG
    emu.gfx = True
    emu.stub('o_07_176e', 0)

    def copy_work_page(uc, addr, size, d):
        if emu.loaded_ovl == 7:
            emu.page[2][:] = emu.page[1]
    a = emu.lin(OVL_SEG, 0x0dd5)
    emu.uc.hook_add(UC_HOOK_CODE, copy_work_page, begin=a, end=a)
    emu.uc.ctl_remove_cache(0, 0x100000)
    world = (ctypes.c_int * 4).in_dll(port.lib, 'world')
    choices = (ctypes.c_int * 3)(0, 0, 0)
    n = 0
    for sd in range(4):
        emu.reset()
        port.fresh_state()
        seed(emu, port, 2024 + sd)
        for k in range(4):
            emu.ww(emu.ds(0x7e76 + 2 * k), 1)
            world[k] = 1
        emu.ww(emu.ds(0x6780), 0)
        port.var('land_mass').value = 0
        emu.call('o_07_0012', timeout_s=600)
        port.generate_world()
        emu.call('o_05_0000', timeout_s=600)
        port.new_game(choices)
        compare_state(emu, port, 'new game seed %d' % sd)
        n += 1
    emu.gfx = False
    return '%d games' % n


def set_option_word(port, value):
    """unk_19c0 (game options) in the port."""
    import struct
    data = bytearray(port.save_bytes())
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    from sve_layout import SVE_LAYOUT
    o = 0
    for seg, a, n in SVE_LAYOUT:
        if a == '0x19c0':
            data[o:o + 2] = struct.pack('<H', value & 0xFFFF)
        o += n
    port.load_bytes(bytes(data))


@test
def city_views(emu, port):
    """o_19_0000 up to the end of its layout: the city view re-seeds the random generator
    from the city's name and lays the city out with it (city_view_layout)."""
    n = 0
    for sve, mp in saves():
        start(emu, port, sve, mp)
        for c in city_ids(sve):
            seed(emu, port, 1000 + c)
            emu.call('o_19_0000', c, -1)
            port.lib.city_view_layout(c, None, None)
            compare_state(emu, port, '%s city %d' % (os.path.basename(sve), c))
            n += 1
    return '%d cities' % n


@test
def newspapers(emu, port):
    """o_21_0000 up to its last random number: the crowd, the paper's name, the headline."""
    n = 0
    for sve, mp in saves():
        start(emu, port, sve, mp)
        for c in [-2, -1] + city_ids(sve)[:6]:
            seed(emu, port, 77 + n)
            emu.call('o_21_0000', c)
            port.lib.newspaper(c, b'')
            compare_state(emu, port, '%s newspaper %d' % (os.path.basename(sve), c))
            n += 1
    return '%d papers' % n


@test
def spaceships(emu, port):
    """o_18_0d3f: computer players build spaceship parts one after another (with the
    figures of o_18_0249 after each); the ship screen is not shown to the human."""
    n = 0
    for sve, mp in saves():
        data = open(sve, 'rb').read()
        alive, human = data[0x0c], data[2]
        for civ in range(1, 8):
            if not alive & (1 << civ) or civ == human:
                continue
            start(emu, port, sve, mp)
            seed(emu, port, 31 * civ)
            # no ship screens for computer players (their animation runs until a key)
            opt = emu.rw(emu.ds(0x19c0)) & ~32
            emu.ww(emu.ds(0x19c0), opt)
            set_option_word(port, opt)
            for k in range(36):
                kind = (0, 0, 1, 0, 2, 1)[k % 6]
                a = emu.call('o_18_0d3f', civ, kind)
                b = port.spaceship_part_ai(civ, kind)
                what = '%s civ %d part %d' % (os.path.basename(sve), civ, k)
                assert a == b, '%s: result original %d port %d' % (what, a, b)
                compare_state(emu, port, what)
                n += 1
    return '%d parts' % n


@test
def meetings(emu, port):
    """o_06_0000: the human meets each rival (by contact and by diplomat); the first dialogs
    answered from a script, the rest with their first line."""
    import ctypes
    n = 0
    script_arr = (ctypes.c_int * 16).in_dll(port.lib, 'headless_script')
    for script in ((), (1,), (0, 1), (0, 0, 1), (0, 1, 1), (1, 0, 1)):
        state = {'k': 0}

        def answer(e, a, script=script, state=state):
            k = state['k']
            state['k'] += 1
            return script[k] if k < len(script) else 0
        emu.stub('f_1d1e_003f', answer)
        for sve, mp in saves():
            data = open(sve, 'rb').read()
            alive, human = data[0x0c], data[2]
            for civ in range(1, 8):
                if not alive & (1 << civ) or civ == human:
                    continue
                o = 0x26c0 + human * 1536
                x, y = data[o + 1], data[o + 2]
                for by_diplomat in (0, 1):
                    start(emu, port, sve, mp)
                    seed(emu, port, 600 + civ + 10 * by_diplomat)
                    state['k'] = 0
                    for i, v in enumerate(script):
                        script_arr[i] = v
                    port.var('headless_script_len').value = len(script)
                    port.var('headless_script_pos').value = 0
                    what = '%s civ %d diplomat %d script %s' % (os.path.basename(sve), civ, by_diplomat, script)
                    try:
                        emu.call('o_06_0000', civ, x, y, by_diplomat, 0, timeout_s=30)
                    except RuntimeError as e:
                        raise AssertionError('%s: original: %s' % (what, e))
                    port.diplomacy_meeting(civ, x, y, by_diplomat)
                    assert state['k'] == port.var('headless_script_pos').value or (
                        state['k'] >= len(script) and port.var('headless_script_pos').value >= len(script)), \
                        '%s: dialogs differ (original %d, port %d)' % (what, state['k'], port.var('headless_script_pos').value)
                    compare_state(emu, port, what)
                    n += 1
    emu.stub('f_1d1e_003f', 0)
    port.var('headless_script_len').value = 0
    return '%d meetings' % n



def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ''
    port = Port()
    failed = 0
    for fn in TESTS:
        if only not in fn.__name__:
            continue
        # A fresh emulator per test: Unicorn may keep translated overlay code across tests.
        emu = new_emu()
        t = time.time()
        try:
            info = fn(emu, port)
            print('PASS %-24s %s (%.1fs)' % (fn.__name__, info or '', time.time() - t))
        except AssertionError as e:
            failed += 1
            print('FAIL %-24s %s' % (fn.__name__, e))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
