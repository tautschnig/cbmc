#!/usr/bin/env python3
"""Compare the shell front end's pattern matching with real shells.

For random patterns, which may contain a quoted expansion "$d", and random
values of x and d, computes case and ${x#p}, ${x##p}, ${x%p}, ${x%%p} with
dash, and generates scripts that assert these results, one per pattern.
CBMC checks them with --shell-strings refined and native. Checks on which
bash or busybox disagree with dash are left out.

Usage: pattern_differential.py CBMC [MODE...] [--patterns N] [--jobs N]
MODE is refined or native (default: both).
"""

import argparse
import os
import random
import sys
import tempfile

from shell_differential import (MODES, check_scripts, chunked, preflight,
                                quote, report, run_shell)

OPERATIONS = ['case', '#', '##', '%', '%%']
SHELLS = [('dash', ['dash']), ('bash', ['bash', '--posix']),
          ('busybox', ['busybox', 'sh'])]


def make_inputs(count):
    random.seed(3)
    alphabet = list('ab*?[]!-') + ['\\*', '[!a]', '[a-c]', '[]]',
                                    '[[:alpha:]]', '"$d"']
    patterns = sorted({''.join(random.choice(alphabet)
                               for _ in range(random.randint(0, 4)))
                       for _ in range(count)})
    subjects = sorted({''.join(random.choice('ab-]![*c')
                               for _ in range(random.randint(0, 5)))
                       for _ in range(16)})
    values = ['a', '', 'b*']
    arms = [(s, values[i % len(values)]) for i, s in enumerate(subjects)]
    return patterns, arms


def case_word(p):
    return p if p else '""'


def operation(p, op):
    """The shell text of operation op with pattern p on x."""
    if op == 'case':
        return f'case "$x" in {case_word(p)}) r=y ;; *) r=n ;; esac'
    return f'r="${{x{op}{p}}}"'


def shell_results(shell, patterns, arms):
    """The results of the operations, by pattern, arm, and operation."""
    lines = []
    for p in patterns:
        for s, d in arms:
            line = f'x={quote(s)}; d={quote(d)}; '
            for op in OPERATIONS:
                line += operation(p, op) + '; printf "%s|" "$r"; '
            lines.append(line + 'echo')
    out = run_shell(shell, '\n'.join(lines) + '\n').splitlines()
    assert len(out) == len(lines), (shell, len(out), len(lines))
    keys = [(p, arm) for p in patterns for arm in arms]
    return {key: line.split('|')[:len(OPERATIONS)]
            for key, line in zip(keys, out)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('cbmc')
    parser.add_argument('modes', nargs='*', default=list(MODES))
    parser.add_argument('--patterns', type=int, default=400)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--memory', type=int, default=4 << 30)
    parser.add_argument('--work', help='keep the scripts in this directory')
    # CBMC needs about one solver run per arm with a failed property, and
    # each run solves the whole script: the time of a script grows with the
    # square of its arms, which the refined string solver feels most.
    parser.add_argument('--parts', type=int, nargs=2, default=[2, 1],
                        metavar=('REFINED', 'NATIVE'),
                        help='scripts per pattern for each mode')
    args = parser.parse_args()

    patterns, arms = make_inputs(args.patterns)
    results = {name: shell_results(shell, patterns, arms)
               for name, shell in SHELLS}
    # Scripts of the values of one pattern: a pattern that the front end
    # does not support only fails its own scripts.
    all_arms, disagreements = [], 0
    for p in patterns:
        for s, d in arms:
            items = [('setup', f'x={quote(s)}; d={quote(d)}')]
            expected = results['dash'][(p, (s, d))]
            for k, op in enumerate(OPERATIONS):
                if any(results[name][(p, (s, d))][k] != expected[k]
                       for name, _ in SHELLS):
                    disagreements += 1
                    continue
                items.append((
                    'value', operation(p, op), quote(expected[k]),
                    f'x={s!r} d={d!r} {op} {p!r}: {expected[k]!r}'))
            all_arms.append(items)
    print(f'{len(patterns)} patterns x {len(arms)} values x '
          f'{len(OPERATIONS)} operations; {disagreements} checks left out '
          'as the shells disagree')

    failed = False
    with tempfile.TemporaryDirectory() as temporary:
        work = args.work or temporary
        os.makedirs(work, exist_ok=True)
        preflight(args.cbmc, args.modes, work)
        for mode in args.modes:
            # The largest divisor of len(arms) that is at most
            # len(arms) / parts (rounded up), so that no script mixes
            # patterns.
            parts = dict(zip(MODES, args.parts))[mode]
            size = next(k for k in range(-(-len(arms) // parts), 0, -1)
                        if len(arms) % k == 0)
            failed |= report(mode, *check_scripts(
                args.cbmc, mode, all_arms, chunked(size),
                work, args.jobs, args.timeout, args.memory))
            sys.stdout.flush()
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
