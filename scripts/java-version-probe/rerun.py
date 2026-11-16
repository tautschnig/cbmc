#!/usr/bin/env python3
# rerun.py CHK_DIR JBMC [METHOD_REGEX]: reruns JBMC on the harnesses of the cases whose verdicts excluded a result, and prints the new verdicts
import json, re, subprocess, sys, concurrent.futures
d, jbmc = sys.argv[1], sys.argv[2]
pat = sys.argv[3] if len(sys.argv) > 3 else '.'
import os
M = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../jbmc/lib/java-models-library/target')
res = json.load(open(f'{d}/results.json'))
todo = [r for r in res if re.search(pat, r['method']) and any(v in ('excluded', 'unreachable') for k, v in r['verdicts'].items() if not k.startswith('jbmc '))]
def run(r):
    c = r['case']
    src = open(f"{d}/c{c}/H.java").read().split('\n')
    cmd = f"cd {d}/c{c} && ulimit -v 6000000 && timeout 120 {jbmc} --function H.m{c} --throw-runtime-exceptions --unwind 40 --unwinding-assertions --cp .:{M}/core-models.jar:{M}/cprover-api.jar H"
    out = subprocess.run(['bash', '-c', cmd], capture_output=True, text=True).stdout
    vs = []
    for i, l in enumerate(src, 1):
        if l.strip().startswith('assert !('):
            m = re.search(rf"\] line {i} assertion[^\n]*: (SUCCESS|FAILURE)", out)
            vs.append('?' if not m else 'excl' if m.group(1) == 'SUCCESS' else 'poss')
    end = re.search(r"\] line 8 assertion[^\n]*: (SUCCESS|FAILURE)", out)
    unw = 'UNWIND' if re.search(r"unwinding assertion.*: FAILURE", out) else ''
    return c, r['method'], vs, ('UNREACH' if end and end.group(1) == 'SUCCESS' else ''), unw
with concurrent.futures.ThreadPoolExecutor(6) as ex:
    for c, m, vs, u, w in ex.map(run, todo):
        print(c, m.replace('java.lang.', ''), ' '.join(vs), u, w)
