"""Transitive callees of a function (from build/asm/functions.txt), skipping UI-only code.

    python3 -I tools/callees.py city_process
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Display / sound / input / text only: never change game state.
UI = re.compile(r'^(f_0000_|f_0181_|f_1d1e_|o_19_|o_21_|o_04_|o_08_|show_message|load_text_section|'
                r'strcpy|strcat|itoa|f_2058_|f_01a7_046e|f_01a7_0252|f_01a7_026a|f_01a7_02a6|f_01a7_0282|'
                r'f_01a7_0296|f_1dde_0098|f_1dde_0335|f_0d07_6f47|f_0d07_6f89|f_0d07_703b|f_0d07_6ec1|'
                r'f_0d07_726d|f_0d07_72d1|f_0d07_7133|f_0d07_6d7a|f_1b05_0004|f_1b05_03b1|f_0402_3e8c|'
                r'f_1b05_11c6|f_0237_1b40|f_1439_08be|_l|abs$|rand$|srand$|f_0860_0006|f_1fb4_)')


def load():
    funcs = {}
    cur = None
    for ln in open(os.path.join(ROOT, 'build/asm/functions.txt')):
        if not ln.startswith(' '):
            p = ln.split()
            cur = p[0]
            calls = p[3][6:].split(',') if len(p) > 3 and p[3].startswith('calls=') else []
            funcs[cur] = (p[1], int(p[2]), [c for c in calls if c])
    return funcs


def main(root):
    funcs = load()
    seen, order = set(), []

    def visit(f):
        if f in seen or UI.match(f):
            return
        seen.add(f)
        for c in funcs.get(f, ('', 0, []))[2]:
            visit(c)
        order.append(f)
    visit(root)
    total = 0
    for f in order:
        unit, n, calls = funcs.get(f, ('?', 0, []))
        total += n
        print('%-28s %-8s %5d' % (f, unit, n))
    print('%d functions, %d instructions' % (len(order), total))


if __name__ == '__main__':
    main(sys.argv[1])
