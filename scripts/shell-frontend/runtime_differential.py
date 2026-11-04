#!/usr/bin/env python3
"""Compare the shell front end's integer conversions with real shells.

For generated strings x, computes with dash and bash the status of
[ "$x" -eq V ] and the value of $((x)), and generates scripts that assert
these results; CBMC checks them with --shell-strings refined and native.

- test: the status (0, 1, or 2 for an error) must be the shell's.
- arithmetic: where the shell accepts x, $((x)) must give the same number;
  where it rejects x, the front end must report a failed property.
Results that the model over-approximates, and values that the shell
accepts but the front end reports, are sound but imprecise: the report
counts them as imprecise and flagged.

Usage: runtime_differential.py CBMC [MODE...] [--jobs N]
MODE is refined or native (default: both).
"""

import argparse
import os
import random
import re
import sys
import tempfile

from shell_differential import (MODES, check_scripts, chunked, preflight,
                                quote, report, run_shell)

WHITE_SPACE = '\n\t\v\r\f'


def make_cases():
    random.seed(7)
    alphabet = list(' -+0123456789xXabfAF8') + list(WHITE_SPACE)
    cases = {'', ' ', '0', '-0', '+0', '00', '08', '010', '0x', '0x1f',
             '0X1F', '-0x10', ' 1', '1 ', '+-1', '--1', '1x',
             '9223372036854775807', '9223372036854775808',
             '-9223372036854775808', '-9223372036854775809',
             '0x7fffffffffffffff', '0x8000000000000000',
             '-0x8000000000000000', '0777777777777777777777',
             '01000000000000000000000', '99999999999999999999',
             ' \n 12 \t'}
    while len(cases) < 3000:
        cases.add(''.join(random.choice(alphabet)
                          for _ in range(random.randint(0, 6))))
    return sorted(cases)


def test_value(case):
    """A number to compare case with: its value if it is a decimal
    integer, else 0. Whatever the value, the shell's status is the
    expected one."""
    m = re.fullmatch(r'\s*([-+]?\d+)\s*', case)
    return int(m.group(1)) if m else 0


def shell_results(shell, cases):
    lines = []
    for case in cases:
        lines.append(
            f'x={quote(case)}; [ "$x" -eq {test_value(case)} ] 2>/dev/null; '
            "printf '%s ' $?; (echo $((x))) 2>/dev/null || echo E")
    out = run_shell([shell], '\n'.join(lines) + '\n').splitlines()
    assert len(out) == len(cases), (shell, len(out), len(cases))
    return [tuple(line.split(' ', 1)) for line in out]


def make_arms(cases, results):
    arms = []
    for case, (status, value) in zip(cases, results):
        items = [('setup', f'x={quote(case)}')]
        items.append((
            'value',
            f'[ "$x" -eq {test_value(case)} ] 2>/dev/null; r=$?', status,
            f'test {case!r} -eq {test_value(case)}: {status}'))
        if value.strip() == 'E':
            items.append(('rejected', 'r=$((x))', f'$(({case!r})): error'))
        else:
            items.append((
                'value', 'r=$((x))', value.strip(),
                f'$(({case!r})): {value.strip()}'))
        arms.append(items)
    return arms


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('cbmc')
    parser.add_argument('modes', nargs='*', default=list(MODES))
    parser.add_argument('--dash', default='dash')
    parser.add_argument('--bash', default='bash')
    # The refined string solver takes time that grows fast with the number
    # of cases in a script.
    parser.add_argument('--per-script', type=int, nargs=2,
                        default=[1, 10], metavar=('REFINED', 'NATIVE'),
                        help='cases per script for each mode')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--memory', type=int, default=4 << 30)
    parser.add_argument('--work', help='keep the scripts in this directory')
    args = parser.parse_args()

    cases = make_cases()
    failed = False
    with tempfile.TemporaryDirectory() as temporary:
        work = args.work or temporary
        os.makedirs(work, exist_ok=True)
        preflight(args.cbmc, args.modes, work)
        for name, shell, shebang in [('dash', args.dash, '#!/bin/sh'),
                                     ('bash', args.bash, '#!/bin/bash')]:
            arms = make_arms(cases, shell_results(shell, cases))
            directory = os.path.join(work, name)
            os.makedirs(directory, exist_ok=True)
            for mode in args.modes:
                per_script = dict(zip(MODES, args.per_script))[mode]
                failed |= report(f'{name} {mode}', *check_scripts(
                    args.cbmc, mode, arms, chunked(per_script, shebang),
                    directory, args.jobs, args.timeout, args.memory))
                sys.stdout.flush()
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
