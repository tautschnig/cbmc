#!/usr/bin/env python3
"""Differential fuzzer for the containers of cbmc's Python front end.

Generates small programs over sets, lists and dicts of strings, runs each
with CPython under several PYTHONHASHSEED values (set order depends on
string hashes), and asks cbmc about every outcome CPython produced:

- unsound: cbmc proves an outcome impossible that CPython produced
  (`assert not (outcome)` verifies);
- imprecise: CPython's outcome is the same for every seed, but cbmc does not
  prove it (`assert outcome` fails or errs);
- ok otherwise.

An outcome is the exception the program's function raised (or "ok") and the
string it returned. Results go to OUT/results.jsonl, a summary to stdout.

Usage: fuzz.py --cbmc CBMC --out DIR [--count N] [--seed S] [--jobs J]
       [--backends refined,native-cvc5] [--seeds-per-case K]
       [--replay results.jsonl]
"""

import argparse
import collections
import concurrent.futures
import json
import os
import random
import resource
import subprocess
import sys

ELEMENTS = ['"a"', '"b"', '"c"', '"ab"', '""']
EXCEPTIONS = ['KeyError', 'AttributeError', 'TypeError', 'ValueError',
              'IndexError']


class Generator:
    """Programs of a few statements on variables s, t (sets), l (a list)
    and d (a dict), returning a string."""

    def __init__(self, rnd):
        self.r = rnd

    def elem(self):
        return self.r.choice(ELEMENTS)

    def elems(self, lo=0, hi=3):
        return [self.elem() for _ in range(self.r.randint(lo, hi))]

    def set_value(self):
        k = self.r.randrange(5)
        if k == 0:
            e = self.elems(1, 3)
            return '{' + ', '.join(e) + '}'
        if k == 1:
            return 'set([' + ', '.join(self.elems(0, 4)) + '])'
        if k == 2:
            return '{x + ' + self.elem() + ' for x in [' + \
                ', '.join(self.elems(1, 3)) + ']}'
        if k == 3:
            return 'set()'
        return '{' + ', '.join(self.elems(1, 2)) + '}'

    def statement(self):
        r = self.r
        k = r.randrange(16)
        if k == 0:
            return f's.add({self.elem()})'
        if k == 1:
            return f's.discard({self.elem()})'
        if k == 2:
            return f's.remove({self.elem()})'
        if k == 3:
            return f's.update({self.set_value()})'
        if k == 4:
            return 's = s.' + r.choice(
                ['union', 'intersection', 'difference',
                 'symmetric_difference']) + '(t)'
        if k == 5:
            return 's = s ' + r.choice(['|', '&', '-', '^']) + ' t'
        if k == 6:
            return 'p = s.pop()'
        if k == 7:
            return 's.' + r.choice(
                ['intersection_update', 'difference_update',
                 'symmetric_difference_update']) + '(t)'
        if k == 8:
            return f'l.append({self.elem()})'
        if k == 9:
            return 'l.extend(s)'
        if k == 10:
            return f'l.remove({self.elem()})'
        if k == 11:
            return f'd[{self.elem()}] = {self.elem()}'
        if k == 12:
            return f'd.pop({self.elem()})'
        if k == 13:
            return 's = set(l)'
        if k == 14:
            return r.choice(['s.foo()', 'l.foo()', 'd.foo()', 'l.add("a")',
                             's.append("a")', 'd.add("a")'])
        return 't = s.copy()'

    def result(self):
        r = self.r
        k = r.randrange(14)
        if k == 0:
            return 'str(len(s))'
        if k == 1:
            return f'"1" if {self.elem()} in s else "0"'
        if k == 2:
            return '"1" if s == t else "0"'
        if k == 3:
            return '"1" if s <= t else "0"'
        if k == 4:
            return '",".join(sorted(s))'
        if k == 5:
            return 'list(s)[0] if s else "-"'
        if k == 6:
            return 'next(iter(s)) if s else "-"'
        if k == 7:
            return 's.pop() if s else "-"'
        if k == 8:
            return '"1" if s.issubset(t) else "0"'
        if k == 9:
            return '"1" if s.isdisjoint(t) else "0"'
        if k == 10:
            return '",".join(l)'
        if k == 11:
            return 'str(len(d))'
        if k == 12:
            return f'"1" if s != {self.set_value()} else "0"'
        return '"1" if s == ' + '[' + ', '.join(self.elems(1, 2)) + '] else "0"'

    def program(self):
        r = self.r
        lines = [f's = {self.set_value()}', f't = {self.set_value()}',
                 'l = [' + ', '.join(self.elems(0, 3)) + ']',
                 'd = {' + ', '.join(f'{self.elem()}: {self.elem()}'
                                     for _ in range(r.randint(0, 2))) + '}']
        lines += [self.statement() for _ in range(r.randint(1, 3))]
        return lines, self.result()


def source(body, result, check):
    """The program: f() runs body and returns result; the module records
    its outcome and asserts check about it."""
    out = ['def f() -> str:']
    out += ['    ' + b for b in body]
    out.append(f'    return {result}')
    out.append('k = "ok"')
    out.append('r = ""')
    out.append('try:')
    out.append('    r = f()')
    for e in EXCEPTIONS:
        out.append(f'except {e}:')
        out.append(f'    k = "{e}"')
    if check is not None:
        out.append(check)
    return '\n'.join(out) + '\n'


def cpython_outcomes(body, result, seeds, work):
    path = os.path.join(work, 'cpython.py')
    with open(path, 'w') as f:
        f.write(source(body, result, 'print(repr((k, r)))'))
    outcomes = set()
    for seed in range(seeds):
        env = dict(os.environ, PYTHONHASHSEED=str(seed))
        p = subprocess.run([sys.executable, path], env=env,
                           capture_output=True, text=True, timeout=30)
        if p.returncode != 0:
            return None  # another exception: not a case
        outcomes.add(eval(p.stdout.strip()))
    return sorted(outcomes)


def outcome_expr(o):
    return f'(k == {json.dumps(o[0])} and r == {json.dumps(o[1])})'


BACKEND_FLAGS = {
    'refined': [],
    'native-cvc5': ['--cvc5', '--python-smt-strings'],
}


def run_cbmc(cbmc, path, flags, timeout, mem_mb):
    def limit():
        resource.setrlimit(resource.RLIMIT_AS,
                           (mem_mb * 1024 * 1024, mem_mb * 1024 * 1024))
    try:
        p = subprocess.run([cbmc, path, '--unwind', '20',
                            '--unwinding-assertions'] + flags,
                           capture_output=True, text=True, timeout=timeout,
                           preexec_fn=limit)
    except subprocess.TimeoutExpired:
        return 'timeout', ''
    out = p.stdout + p.stderr
    for v in ('SUCCESSFUL', 'FAILED', 'ERROR'):
        if f'VERIFICATION {v}' in out:
            return v, out
    return 'crash', out[-2000:]


def check_case(case, cbmc, backend, timeout, mem_mb, out_dir):
    work = os.path.join(out_dir, 'work', f'{case["id"]}-{backend}')
    os.makedirs(work, exist_ok=True)
    flags = BACKEND_FLAGS[backend]
    details = {}
    verdict = 'ok'
    # Each outcome CPython produced must be possible.
    for i, o in enumerate(case['outcomes']):
        path = os.path.join(work, f'possible{i}.py')
        with open(path, 'w') as f:
            f.write(source(case['body'], case['result'],
                           f'assert not {outcome_expr(o)}'))
        v, out = run_cbmc(cbmc, path, flags, timeout, mem_mb)
        details[f'possible{i}'] = v
        if v == 'SUCCESSFUL':
            verdict = 'unsound'
        elif v in ('timeout', 'crash') and verdict == 'ok':
            verdict = v
    # A deterministic outcome should be proved.
    if len(case['outcomes']) == 1 and verdict == 'ok':
        path = os.path.join(work, 'proved.py')
        with open(path, 'w') as f:
            f.write(source(case['body'], case['result'],
                           f'assert {outcome_expr(case["outcomes"][0])}'))
        v, out = run_cbmc(cbmc, path, flags, timeout, mem_mb)
        details['proved'] = v
        if v != 'SUCCESSFUL':
            verdict = {'FAILED': 'imprecise', 'ERROR': 'error'}.get(v, v)
    return dict(case, backend=backend, verdict=verdict, details=details)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cbmc', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--count', type=int, default=200)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--mem-mb', type=int, default=6000)
    ap.add_argument('--seeds-per-case', type=int, default=8)
    ap.add_argument('--backends', default='refined,native-cvc5')
    ap.add_argument('--replay')
    args = ap.parse_args()
    os.makedirs(os.path.join(args.out, 'work'), exist_ok=True)
    cbmc = os.path.abspath(args.cbmc)

    cases = []
    if args.replay:
        seen = set()
        for line in open(args.replay):
            c = json.loads(line)
            if c['id'] not in seen:
                seen.add(c['id'])
                cases.append({k: c[k] for k in
                              ('id', 'body', 'result', 'outcomes')})
    else:
        gen = Generator(random.Random(args.seed))
        while len(cases) < args.count:
            body, result = gen.program()
            outcomes = cpython_outcomes(body, result, args.seeds_per_case,
                                        os.path.join(args.out, 'work'))
            if outcomes is None:
                continue
            cases.append({'id': f'c{len(cases):05d}', 'body': body,
                          'result': result,
                          'outcomes': [list(o) for o in outcomes]})

    backends = args.backends.split(',')
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(check_case, c, cbmc, b, args.timeout,
                               args.mem_mb, args.out)
                   for c in cases for b in backends]
        for fut in concurrent.futures.as_completed(futures):
            results.append(fut.result())
    results.sort(key=lambda x: (x['id'], x['backend']))
    with open(os.path.join(args.out, 'results.jsonl'), 'w') as f:
        for x in results:
            f.write(json.dumps(x) + '\n')

    print(f'{len(cases)} cases, back ends: {", ".join(backends)}')
    for b in backends:
        counts = collections.Counter(x['verdict'] for x in results
                                     if x['backend'] == b)
        print(f'{b:12} ' + '  '.join(
            f'{v} {counts.get(v, 0)}' for v in
            ('ok', 'unsound', 'imprecise', 'error', 'timeout', 'crash')))
    for v in ('unsound', 'crash', 'error'):
        bad = [x for x in results if x['verdict'] == v]
        if bad:
            print(f'== {v}')
            for x in bad[:40]:
                print(f'  {x["id"]} {x["backend"]}: '
                      f'{"; ".join(x["body"])}; return {x["result"]} '
                      f'outcomes={x["outcomes"]}')


if __name__ == '__main__':
    main()
