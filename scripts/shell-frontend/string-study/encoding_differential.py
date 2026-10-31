#!/usr/bin/env python3
"""Check the string encodings of the shell front-end against dash.

Generates scripts that apply case, pattern removal, ${#x}, the test
utility's integer comparison and arithmetic expansion to constant strings,
computes the results with dash, and asserts them with __CPROVER_assert.
Each string representation must then prove every assertion: a FAILURE is a
wrong encoding. Inputs are constants, so this checks the encodings'
semantics, not the solvers' handling of unknown strings.

Arithmetic on values the front-end does not support (octal or hexadecimal
constants, for instance) fails the shell-arithmetic property instead; that
is sound, and counted separately.

Usage: encoding_differential.py CBMC [CASES] [SEED]
"""

import collections
import os
import random
import re
import subprocess
import sys
import tempfile

CONFIGS = {
    'bounded': ['--shell-strings', 'bounded'],
    'refined': ['--shell-strings', 'refined', '--refine-strings'],
    'native-z3': ['--shell-strings', 'native', '--z3'],
    'native-cvc5': ['--shell-strings', 'native', '--cvc5'],
}
# Encodings the unbounded representations do not support (conversion
# errors), by feature.
UNSUPPORTED = {
    'refined': {'question', 'removal-multi'},
    'native-z3': {'removal-multi'},
    'native-cvc5': {'removal-multi'},
}
BATCH = 40


def quote(s):
    return "'" + s.replace("'", "'\\''") + "'"


def random_pattern(rng, kind):
    """A pattern over a, b and '.', with stars and, for 'question', ? and
    bracket expressions."""
    n = rng.randint(0, 4)
    parts = [rng.choice('ab.') for _ in range(n)]
    stars = {'removal-1': 1, 'match-multi': rng.randint(2, 3),
             'removal-multi': 2, 'question': rng.randint(0, 2)}[kind]
    for _ in range(stars):
        parts.insert(rng.randint(0, len(parts)), '*')
    if kind == 'question':
        for _ in range(rng.randint(1, 2)):
            parts.insert(rng.randint(0, len(parts)),
                         rng.choice(['?', '[ab]', '[!a]', '[.-b]']))
    return ''.join(parts)


def random_subject(rng):
    return ''.join(rng.choice('ab.') for _ in range(rng.randint(0, 7)))


def random_number(rng):
    """Strings around the syntax of integers."""
    choice = rng.random()
    if choice < 0.4:
        s = str(rng.choice([0, 1, 7, 42, 100, 2 ** 31, 2 ** 63 - 1,
                            rng.randint(0, 10 ** rng.randint(1, 19))]))
    elif choice < 0.6:
        s = rng.choice(['-', '+']) + str(rng.randint(0, 1000))
    else:
        s = ''.join(rng.choice('0123456789 -+x\t') for _ in
                    range(rng.randint(0, 5)))
    if rng.random() < 0.2:
        s = rng.choice([' ', '\t', '']) + s + rng.choice([' ', '\t', ''])
    if rng.random() < 0.05:
        s = '9223372036854775808'
    if rng.random() < 0.05:
        s = '0' + str(rng.randint(1, 77))
    return s


def dash(script):
    r = subprocess.run(['dash', '-c', script], capture_output=True,
                       text=True, timeout=10)
    return r.stdout


def make_cases(rng, count):
    """(feature, setup, value-expression or None, expected) tuples."""
    cases = []
    for _ in range(count):
        kind = rng.choice(['removal-1', 'match-multi', 'question',
                           'removal-multi', 'length', 'test-int',
                           'arith'])
        x = random_subject(rng)
        if kind in ('removal-1', 'removal-multi', 'match-multi',
                    'question'):
            p = random_pattern(rng, kind)
            if kind.startswith('removal'):
                op = rng.choice(['#', '##', '%', '%%'])
                expr = '${x' + op + p + '}'
                out = dash(f'x={quote(x)}; printf %s "{expr}"')
                cases.append((kind, f'x={quote(x)}', f'"{expr}"', out))
            else:
                script = f'x={quote(x)}; case $x in {p}) printf 1;; ' \
                         f'*) printf 0;; esac'
                out = dash(script)
                cases.append((kind, f'x={quote(x)}',
                              f'"$(case_{len(cases)})"', out, p))
        elif kind == 'length':
            cases.append((kind, f'x={quote(x)}', '"${#x}"', str(len(x))))
        elif kind == 'test-int':
            s = random_number(rng)
            out = dash(f'x={quote(s)}; [ "$x" -eq 7 ] 2>/dev/null; '
                       f'printf %s $?')
            cases.append((kind, f'x={quote(s)}', None, out))
        else:
            s = random_number(rng)
            out = dash(f'x={quote(s)}; printf %s "$((x + 1))"'
                       ' 2>/dev/null')
            cases.append((kind, f'x={quote(s)}', '"$((x + 1))"', out))
    return cases


def script_for(cases, config):
    lines = ['#!/bin/sh']
    kept = []
    for case in cases:
        kind = case[0]
        if kind in UNSUPPORTED.get(config, ()):
            continue
        if kind == 'arith' and case[3] == '':
            # dash rejects the value; the front-end reports it.
            continue
        kept.append(case)
        lines.append(case[1])
        if kind in ('match-multi', 'question'):
            lines.append(f'case $x in {case[4]}) m=1 ;; *) m=0 ;; esac')
            lines.append(f'__CPROVER_assert "$m" = {case[3]}')
        elif kind == 'test-int':
            lines.append('[ "$x" -eq 7 ]')
            lines.append(f'__CPROVER_assert "$?" = {case[3]}')
        else:
            lines.append(f'__CPROVER_assert {case[2]} = {quote(case[3])}')
    return '\n'.join(lines) + '\n', kept


def edge_blank(script, line):
    """Whether the value of x assigned before the given line starts or ends
    with white space, or has more than 18 bytes: refined strings
    over-approximate such integers."""
    for text in reversed(script.split('\n')[:line - 1]):
        if text.startswith('x='):
            value = text[2:].strip("'")
            return value != value.strip(' \t\n\v\f\r') or len(value) > 18
    return False


def run(cbmc, config, script, work):
    path = os.path.join(work, f'{config}.sh')
    with open(path, 'w') as f:
        f.write(script)
    cmd = ['bash', '-c',
           'ulimit -v 16000000; exec timeout 300 "$0" "$@"', cbmc] + \
        CONFIGS[config] + [path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = r.stdout + r.stderr
    results = []
    for m in re.finditer(r'\[(\S+)\] file \S+ line (\d+) (.*): '
                         r'(SUCCESS|FAILURE)', out):
        results.append((int(m.group(2)), m.group(1), m.group(3), m.group(4)))
    if not results:
        return None, out[-2000:]
    return results, out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cbmc = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    cases = make_cases(rng, count)
    totals = {c: collections.Counter() for c in CONFIGS}
    with tempfile.TemporaryDirectory() as work:
        for start in range(0, len(cases), BATCH):
            batch = cases[start:start + BATCH]
            for config in CONFIGS:
                script, kept = script_for(batch, config)
                results, out = run(cbmc, config, script, work)
                if results is None:
                    totals[config]['error'] += len(kept)
                    print(f'{config}: no results for batch {start}:\n{out}')
                    continue
                for line, prop, desc, status in results:
                    text = script.split('\n')[line - 1]
                    if not prop.startswith('assertion') and \
                            not prop.startswith('shell-arithmetic'):
                        continue
                    if status == 'SUCCESS':
                        totals[config]['ok' if prop.startswith('assertion')
                                       else 'arith-check ok'] += 1
                    elif prop.startswith('shell-arithmetic'):
                        totals[config]['unsupported value'] += 1
                    elif config == 'refined' and edge_blank(script, line):
                        # Refined strings over-approximate integers with
                        # blanks around them or of more than 18 bytes.
                        totals[config]['imprecise (blanks, long)'] += 1
                    else:
                        totals[config]['MISMATCH'] += 1
                        setup = script.split('\n')[line - 2]
                        if 'case' in setup:
                            setup = script.split('\n')[line - 3] + '; ' + \
                                setup
                        print(f'{config}: MISMATCH [{prop}] {setup!r} '
                              f'{text!r}')
    for config, counter in totals.items():
        print(f'{config:12} ' + ', '.join(f'{k}: {v}' for k, v in
                                          sorted(counter.items())))


if __name__ == '__main__':
    main()
