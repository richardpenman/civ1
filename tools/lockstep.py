"""Play the original DOS game and the SDL port side by side with the same input and the same
random numbers, and report where they differ.

    python3 -I tools/lockstep.py --slot 0 --keys "KP6 ENTER M:85,3 ESC ..." [--out DIR]

The original runs whole in tools/dosmachine.py: setup, intro, the title menu, "Load a Saved
Game", CIVILn.SVE.  Its random state is set to --seed at the load menu; the port loads the
same save with --play and the same seed.  From then on each script token is given to both
where the game waits for input, and at every such wait both sides record the screen, the
saved game bytes and the random state.  Step N is the state at the Nth wait (step 0: the map
just after loading); the token between step N and N+1 is script token N.

Tokens: ENTER ESC SPACE BKSP TAB UP DOWN LEFT RIGHT HOME END PGUP PGDN KP0-KP9 F1-F10, a
letter or digit, M:x,y (a left click at screen pixel x,y) and R:x,y (a right click).  Move units with the arrow
and HOME/END/PGUP/PGDN tokens (the keypad with NumLock off); KPn is the keypad with NumLock
on, which the original treats differently (KP6 scrolls the map).

The report (DIR/report.html) shows for each step both screens and the pixels that differ,
and the saved game fields that differ.
"""
import argparse, os, pickle, shutil, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import dosmachine as D  # noqa: E402

SETUP = '1 1 1 SPACE DOWN ENTER ENTER'.split()    # VGA, no sound, skip intro, Load, drive C:


# ---- the original ----------------------------------------------------------------------
def boot_original(cache, setup=SETUP):
    """The machine waiting at the "Select Load File" list, or at the title menu with
    setup=SETUP[:4] (cached: the boot takes a while)."""
    if os.path.exists(cache):
        with open(cache, 'rb') as f:
            return D.DosMachine.from_snapshot(pickle.load(f))
    m = D.DosMachine()
    m.load_exe('CIV.EXE')
    for k in setup:
        if not m.run_until_wait():
            raise RuntimeError('the original stopped during setup')
        m.keys.append(D.key_code(k))
    if not m.run_until_wait():
        raise RuntimeError('the original stopped before the load menu')
    with open(cache, 'wb') as f:
        pickle.dump(m.snapshot(), f)
    return m


def give_original(m, tok):
    if tok.startswith('M:') or tok.startswith('R:'):
        x, y = (int(v) for v in tok[2:].split(','))
        if tok[0] == 'M':
            m.mouse_event(x, y, 1, 3)
            m.mouse_event(x, y, 0, 4)
        else:
            m.mouse_event(x, y, 2, 9)
            m.mouse_event(x, y, 0, 16)
    else:
        m.keys.append(D.key_code(tok))


def run_original(slot, seed, script, out, cache_dir):
    os.makedirs(out, exist_ok=True)
    if slot < 0:                                   # --newgame: from the title menu
        m = boot_original(os.path.join(cache_dir, 'orig_title.snap'), SETUP[:4])
        m.set_rng(seed)
        with open(os.path.join(out, 'world_seed'), 'w') as f:    # (set at its start: the port needs it)
            f.write('%d\n' % m.rw((m.LOAD + 0x2A1B) * 16 + 0x6e00))
    else:
        m = boot_original(os.path.join(cache_dir, 'orig_loadmenu.snap'))
        m.set_rng(seed)
        for k in ['DOWN'] * slot + ['ENTER']:
            m.keys.append(D.key_code(k))
            if not m.run_until_wait():
                raise RuntimeError('the original stopped while loading')
    for n in range(len(script) + 1):
        pix, pal = m.screen()
        with open(os.path.join(out, '%04d.scr' % n), 'wb') as f:
            f.write(pix)
            f.write(b''.join(struct.pack('<I', 0xFF000000 | pal[3 * i] << 16 | pal[3 * i + 1] << 8 | pal[3 * i + 2])
                             for i in range(256)))
        with open(os.path.join(out, '%04d.sve' % n), 'wb') as f:
            f.write(m.save_bytes())
        dg = (m.LOAD + 0x2A1B) * 16
        with open(os.path.join(out, '%04d.rng' % n), 'w') as f:
            f.write('%d\nview %d %d\n' % (m.rng_state(), m.rw(dg + 0xe3ec), m.rw(dg + 0xe67e)))
        if n == len(script):
            break
        give_original(m, script[n])
        if not m.run_until_wait():
            print('original: stopped after token %d (%s)' % (n, script[n]), file=sys.stderr)
            break


# ---- the saved game -------------------------------------------------------------------
SCENARIO = None       # --scenario DIR: CIVILn.SVE/.MAP played instead of the original's


def save_paths(slot):
    d = SCENARIO or D.ORIG
    return os.path.join(d, 'CIVIL%d.SVE' % slot), os.path.join(d, 'CIVIL%d.MAP' % slot)


def install_scenario(slot):
    """The original reads its files from the scratch directory first."""
    scratch = os.path.join(ROOT, 'build', 'dos_scratch')
    os.makedirs(scratch, exist_ok=True)
    for f in os.listdir(scratch):                  # (saves and scenarios of earlier runs)
        if f.upper().startswith('CIVIL'):
            os.remove(os.path.join(scratch, f))
    for ext in ('SVE', 'MAP'):
        if SCENARIO:
            shutil.copy(os.path.join(SCENARIO, 'CIVIL%d.%s' % (slot, ext)),
                        os.path.join(scratch, 'CIVIL%d.%s' % (slot, ext)))


# ---- the port --------------------------------------------------------------------------
def run_port(slot, seed, script, out):
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, CIV_HEADLESS='1', CIV_LOCKSTEP=out, CIV_SEED=str(seed), CIV_KEYS=' '.join(script))
    ws = os.path.join(os.path.dirname(out), 'orig', 'world_seed')
    if os.path.exists(ws):
        env['CIV_WORLD_SEED'] = open(ws).read().strip()
    args = [os.path.join(ROOT, 'civ'), '--data', D.ORIG, '--saves', os.path.join(out, 'saves')]
    if slot >= 0:
        args += ['--play'] + list(save_paths(slot))
    r = subprocess.run(args,
                       env=env, cwd=ROOT, capture_output=True, text=True, timeout=600)
    if r.returncode:
        print('port: exit %d\n%s' % (r.returncode, r.stderr[-2000:]), file=sys.stderr)


# ---- comparing -------------------------------------------------------------------------
def load_step(d, n):
    p = os.path.join(d, '%04d' % n)
    if not os.path.exists(p + '.scr'):
        return None
    raw = open(p + '.scr', 'rb').read()
    pal = struct.unpack('<256I', raw[64000:64000 + 1024])
    rgb = [((c >> 18) & 63, (c >> 10) & 63, (c >> 2) & 63) for c in pal]    # 6 bits, like the DAC
    sve = open(p + '.sve', 'rb').read() if os.path.exists(p + '.sve') else b''
    lines = open(p + '.rng').read().split('\n') if os.path.exists(p + '.rng') else ['?']
    rng = lines[0].strip()
    view = lines[1].strip() if len(lines) > 1 else ''
    return {'pix': raw[:64000], 'rgb': rgb, 'pal': pal, 'sve': sve, 'rng': rng, 'view': view}


def save_fields():
    """byte offset of a .SVE -> field name (libciv's save_field_name)"""
    import ctypes
    lib = ctypes.CDLL(os.path.join(ROOT, 'libciv.so'))
    buf = ctypes.create_string_buffer(64)

    def name(pos):
        lib.save_field_name(ctypes.c_long(pos), buf, ctypes.c_size_t(64))
        return buf.value.decode()
    return name


def compare(a, b, fields):
    """(pixels differing, bounding box, [(field, orig bytes, port bytes)])"""
    diff = []
    for i in range(64000):
        if a['rgb'][a['pix'][i]] != b['rgb'][b['pix'][i]]:
            diff.append(i)
    box = None
    if diff:
        xs = [i % 320 for i in diff]
        ys = [i // 320 for i in diff]
        box = (min(xs), min(ys), max(xs), max(ys))
    fd = {}
    for i in range(min(len(a['sve']), len(b['sve']))):
        if a['sve'][i] != b['sve'][i]:
            name = fields(i)
            fd.setdefault(name, []).append(i)
    fdiffs = [(k, a['sve'][v[0]:v[-1] + 1].hex(), b['sve'][v[0]:v[-1] + 1].hex()) for k, v in fd.items()]
    return diff, box, fdiffs


def write_png(path, step, mark=None):
    from PIL import Image
    im = Image.frombytes('P', (320, 200), step['pix'])
    im.putpalette([v for c in step['pal'] for v in ((c >> 16) & 255, (c >> 8) & 255, c & 255)])
    im = im.convert('RGB')
    if mark:
        px = im.load()
        for i in mark:
            r, g, b = px[i % 320, i // 320]
            px[i % 320, i // 320] = (255, 0, 255)
    im.save(path)


def report(script, odir, pdir, out):
    import html
    fields = save_fields()
    img = os.path.join(out, 'img')
    os.makedirs(img, exist_ok=True)
    rows, summary = [], []
    first = None
    n = 0
    while True:
        a, b = load_step(odir, n), load_step(pdir, n)
        if (a is None and b is None) or n > len(script):
            break
        tok = script[n - 1] if n else '(loaded)'
        if a is None or b is None:
            line = 'step %d after %s: only the %s is still waiting for input' % (n, tok, 'port' if a is None else 'original')
            summary.append(line)
            rows.append('<h2>%s</h2>' % html.escape(line))
            for s, name in ((a, 'orig'), (b, 'port')):
                if s:
                    write_png(os.path.join(img, '%04d_%s.png' % (n, name)), s)
                    rows.append('<img src="img/%04d_%s.png">' % (n, name))
            n += 1
            continue
        diff, box, fdiffs = compare(a, b, fields)
        same = not diff and not fdiffs and a['rng'] == b['rng']
        line = 'step %d after %-8s pixels %5d %s  fields %d  rng %s  %s' % (
            n, tok, len(diff), 'box %s' % (box,) if box else '', len(fdiffs),
            'same' if a['rng'] == b['rng'] else '%s/%s' % (a['rng'], b['rng']),
            a['view'] if a['view'] == b['view'] else '%s/%s' % (a['view'], b['view']))
        summary.append(line)
        if not same and first is None:
            first = n
        write_png(os.path.join(img, '%04d_orig.png' % n), a)
        write_png(os.path.join(img, '%04d_port.png' % n), b)
        write_png(os.path.join(img, '%04d_diff.png' % n), b, diff)
        rows.append('<h2 class="%s">%s</h2>' % ('same' if same else 'diff', html.escape(line)))
        rows.append('<div class="row"><figure><img src="img/%04d_orig.png"><figcaption>original</figcaption></figure>'
                    '<figure><img src="img/%04d_port.png"><figcaption>port</figcaption></figure>'
                    '<figure><img src="img/%04d_diff.png"><figcaption>port, differing pixels magenta</figcaption>'
                    '</figure></div>' % (n, n, n))
        if fdiffs:
            rows.append('<table><tr><th>field</th><th>original</th><th>port</th></tr>%s</table>' % ''.join(
                '<tr><td>%s</td><td>%s</td><td>%s</td></tr>' % (html.escape(k), x[:64], y[:64]) for k, x, y in fdiffs[:40]))
            if len(fdiffs) > 40:
                rows.append('<p>... %d more fields</p>' % (len(fdiffs) - 40))
        n += 1
    page = ('<!doctype html><meta charset="utf-8"><title>Lockstep report</title><style>'
            'body{font:14px sans-serif;background:#fff;color:#111;margin:16px}'
            'img{width:640px;image-rendering:pixelated;max-width:100%%}.row{display:flex;gap:8px;flex-wrap:wrap}'
            'figure{margin:0}h2{font:600 15px monospace}h2.diff{color:#b00}h2.same{color:#070}'
            'table{border-collapse:collapse;font:12px monospace}td,th{border:1px solid #ccc;padding:2px 6px}'
            '</style><h1>Original vs port</h1><p>script: %s</p><pre>%s</pre>%s' % (
                html.escape(' '.join(script)), html.escape('\n'.join(summary)), '\n'.join(rows)))
    with open(os.path.join(out, 'report.html'), 'w') as f:
        f.write(page)
    return summary, first


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--slot', type=int, default=0)
    ap.add_argument('--seed', type=int, default=12345)
    ap.add_argument('--keys', default='')
    ap.add_argument('--keys-file')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'lockstep'))
    ap.add_argument('--port-only', action='store_true', help='rerun only the port (after a fix)')
    ap.add_argument('--newgame', action='store_true', help='start at the title menu instead of a saved game')
    ap.add_argument('--scenario', help='a directory with CIVILn.SVE/.MAP to play instead of the original\'s')
    a = ap.parse_args()
    global SCENARIO
    SCENARIO = a.scenario and os.path.abspath(a.scenario)
    if a.newgame:
        a.slot = -1
    script = a.keys.split()
    if a.keys_file:
        script += open(a.keys_file).read().split()
    odir, pdir = os.path.join(a.out, 'orig'), os.path.join(a.out, 'port')
    t = time.time()
    if not a.port_only:
        shutil.rmtree(odir, ignore_errors=True)
        install_scenario(a.slot)
        run_original(a.slot, a.seed, script, odir, a.out)
        print('original: %.0fs' % (time.time() - t), file=sys.stderr)
    shutil.rmtree(pdir, ignore_errors=True)
    run_port(a.slot, a.seed, script, pdir)
    summary, first = report(script, odir, pdir, a.out)
    print('\n'.join(summary))
    print('first difference: %s' % ('step %d' % first if first is not None else 'none'))
    print('report: %s' % os.path.join(a.out, 'report.html'))


if __name__ == '__main__':
    main()
