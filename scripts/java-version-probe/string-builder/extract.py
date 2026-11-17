import re, sys, collections
def methods(path):
    src = open(path).read()
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)
    out = {}
    for m in re.finditer(r'\n    (public|private|static|final|synchronized|\s)*[\w<>\[\]]+ (\w+)\(([^)]*)\)[^{;]*\{', src):
        name = m.group(2); params = re.sub(r'\s+', ' ', m.group(3)).strip()
        params = ','.join(p.strip().rsplit(' ', 1)[0] for p in params.split(',')) if params else ''
        i = m.end(); depth = 1
        while depth and i < len(src):
            if src[i] == '{': depth += 1
            elif src[i] == '}': depth -= 1
            i += 1
        body = src[m.end():i-1]
        lines = [l.strip() for l in body.split('\n') if re.search(r'check|throw|Preconditions|null|< 0|> count|>= count|length\(\)', l)]
        out[f"{name}({params})"] = ' | '.join(lines)
    return out
vers = sys.argv[2:]
cls = sys.argv[1]
data = {}
for v in vers:
    p = f"{v}/java/lang/{cls}.java" if v == '8' else f"{v}/java.base/java/lang/{cls}.java"
    data[v] = methods(p)
keys = sorted(set().union(*[set(d) for d in data.values()]))
for k in keys:
    groups = collections.OrderedDict()
    for v in vers:
        groups.setdefault(data[v].get(k, 'ABSENT'), []).append(v)
    print(f"### {k}")
    for body, vs in groups.items():
        print(f"  [{','.join(vs)}] {body[:600]}")
