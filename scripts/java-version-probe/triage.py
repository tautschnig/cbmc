import json, sys, collections, re
res = json.load(open(sys.argv[1]))
rows = collections.defaultdict(list)
for r in res:
    v = r['verdicts']
    vers = {a: b for a, b in v.items() if not a.startswith('jbmc ')}
    tri = {a[5:]: b for a, b in v.items() if a.startswith('jbmc ')}
    if all(x in ('possible', 'n/a') for x in vers.values()): continue
    flags = []
    if tri.get('throws') == 'no': flags.append('NEVER-THROWS')
    if tri.get('returns') == 'no': flags.append('never-returns')
    for k, x in tri.items():
        if k.startswith('throws ') and x == 'no': flags.append('not:' + k[7:].split('.')[-1])
    if tri.get('null message') == 'yes': flags.append('null-msg-possible')
    st = ','.join(sorted(set(vers.values())))
    rows[r['method']].append((st, ' '.join(flags)))
for m, l in sorted(rows.items()):
    c = collections.Counter(l)
    print(f"{m.replace('java.lang.','')[:70]:70} " + "; ".join(f"{a}[{b}]x{n}" for (a, b), n in c.items()))
