"""Generate jbmc-strings/StringIndexMessages/Test.java from the probe
results ({8..25}.txt of P.java): one method per Java version asserting
what the JDK of that version gives for each case."""
import re, sys
out = sys.argv[1]
vs = list(range(8, 26))
res = {v: dict(l.rstrip('\n').split(' => ', 1)
               for l in open(f'{v}.txt') if ' => ' in l) for v in vs}

# case key -> Java expression for the result (a String)
cases = {}
strs = {'L1': '"abc"', 'U16': '"a\\u0100c"'}
def add(key, expr):
    cases[key] = expr
for k, s in strs.items():
    for i in (-1, 3, 4):
        add(f'{k} charAt {i}', f'charAt({s}, {i})')
        add(f'{k} codePointAt {i}', f'codePointAt({s}, {i})')
        add(f'{k} codePointBefore {i}', f'codePointBefore({s}, {i})')
        add(f'{k} substring1 {i}', f'substring1({s}, {i})')
    add(f'{k} codePointBefore 0', f'codePointBefore({s}, 0)')
    for b, e in ((-1, 2), (2, 1), (1, 4), (4, 5), (0, 3)):
        n = f'{b},{e}'
        add(f'{k} substring {n}', f'substring({s}, {b}, {e})')
        add(f'{k} codePointCount {n}', f'codePointCount({s}, {b}, {e})')
        add(f'{k} getChars {n} ->0', f'getChars({s}, {b}, {e}, 0)')
        add(f'{k} getBytes {n} ->0', f'getBytes({s}, {b}, {e}, 0)')
    for d in (2, -1):
        add(f'{k} getChars 0,2 ->{d}', f'getChars({s}, 0, 2, {d})')
    add(f'{k} getBytes 0,2 ->2', f'getBytes({s}, 0, 2, 2)')
    add(f'{k} contentEquals sb', f'contentEqualsSb({s})')
    add(f'{k} regionMatches null -1', f'regionMatchesNull({s}, false)')
    add(f'{k} regionMatchesIC null -1', f'regionMatchesNull({s}, true)')
for o, c in ((-1, 1), (1, -1), (2, 2), (4, 0), (3, 0), (1, 2)):
    add(f'valueOf(char[],{o},{c})', f'valueOf({o}, {c})')

def norm(r):
    # JBMC prints neither class names nor helpful NPE messages
    m = re.match(r'value (.*)', r)
    if m:
        return m.group(1)
    cls, msg = r.split(': ', 1)
    if cls == 'java.lang.NullPointerException':
        return "NullPointerException"
    return cls.rsplit('.', 1)[1] + ': ' + msg

def jstr(x):
    return '"' + x.replace('\\', '\\\\').replace('"', '\\"').replace('\u0100', '\\u0100') + '"'

lines = []
for v in vs:
    body = []
    for key, expr in cases.items():
        exp = norm(res[v][key])
        if exp is None:
            continue
        if ' charAt ' in key and 9 <= v <= 17:
            # Latin-1 or UTF-16 by the JVM option CompactStrings
            other = ('U16' if key.startswith('L1') else 'L1') + key[key.index(' '):]
            alt = norm(res[v][other])
            body.append(f'    r = {expr};\n    assert r.equals({jstr(exp)}) || r.equals({jstr(alt)});')
        else:
            body.append(f'    assert {expr}.equals({jstr(exp)});')
    lines.append(f'  public static void java{v}() {{\n    String r;\n' + '\n'.join(body) + '\n  }')
src = open(out + '.in').read().replace('  // GENERATED\n', '\n'.join(lines) + '\n')
open(out, 'w').write(src)
