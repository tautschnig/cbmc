import collections, sys
vs = list(range(8, 26))
d = {v: dict(l.rstrip('\n').split(' => ', 1) for l in open(f'{v}.txt') if ' => ' in l) for v in vs}
keys = [l.split(' => ')[0] for l in open('8.txt') if ' => ' in l]
def rng(g):
    out = []; s = p = g[0]
    for x in g[1:] + [None]:
        if x == p + 1 if x is not None else False: p = x; continue
        out.append(f'{s}' if s == p else f'{s}-{p}')
        if x is not None: s = p = x
    return ','.join(out)
for k in keys:
    groups = collections.OrderedDict()
    for v in vs: groups.setdefault(d[v].get(k), []).append(v)
    print(k)
    for r, g in groups.items(): print(f'   {rng(g):10} {r}')
