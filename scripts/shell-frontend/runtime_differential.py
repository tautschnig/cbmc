#!/usr/bin/env python3
"""Compare the shell runtime's integer conversions with real shells.

Builds the C runtime that src/shell/shell_runtime.cpp generates, runs its
test-utility and arithmetic conversions on generated strings, and checks
them against dash and bash:

- test: where the runtime accepts an operand, `[ "$x" -eq VALUE ]` must
  succeed in the shell; where it rejects one, the shell must exit with 2.
- arithmetic: where the runtime accepts a value, $((x)) must give the
  same number. Rejected values are reported by the verifier as properties,
  which is sound; the script counts those the shell accepts.

Usage: runtime_differential.py BUILD_DIR [DASH] [BASH]
BUILD_DIR is a CMake build directory with the shell front-end built.
"""

import os
import random
import subprocess
import sys
import tempfile

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                   'src')

GENERATOR = r'''
#include <shell/shell_runtime.h>
#include <iostream>
int main() { std::cout << shell_runtime_source(64); }
'''

HARNESS = r'''
#include <stdio.h>
#include <string.h>
#define __CPROVER_assume(x) ((void)0)
#include "runtime.c"
static struct __CPROVER_shell_string mk(const char *s)
{
  struct __CPROVER_shell_string r;
  memset(&r, 0, sizeof r);
  r.set = 1;
  r.length = strlen(s);
  memcpy(r.data, s, r.length);
  return r;
}
int main(int argc, char **argv)
{
  char line[256];
  int any_space = argc > 1 && argv[1][0] == '1';
  while(fgets(line, sizeof line, stdin))
  {
    line[strcspn(line, "\n")] = 0;
    for(char *p = line; *p; ++p)
      *p = *p == 1 ? '\n' : *p == 2 ? '\t' : *p == 3 ? '\v'
         : *p == 4 ? '\r' : *p == 5 ? '\f' : *p;
    long long v1 = 0, v2 = 0;
    _Bool t = __CPROVER_shell_test_integer(mk(line), 1, &v1);
    _Bool a = __CPROVER_shell_arith_integer(mk(line), 1, any_space, &v2);
    printf("%s %lld|%s %lld\n", t ? "T" : "E", t ? v1 : 0, a ? "A" : "E",
           a ? v2 : 0);
  }
}
'''

# Control characters stand for white space in the case file, which is
# line-based.
ENCODED = {'\x01': '\n', '\x02': '\t', '\x03': '\v', '\x04': '\r',
           '\x05': '\f'}


def decode(s):
    return ''.join(ENCODED.get(c, c) for c in s)


def make_cases():
    random.seed(7)
    alphabet = list(' -+0123456789xXabfAF8') + list(ENCODED)
    cases = {'', ' ', '0', '-0', '+0', '00', '08', '010', '0x', '0x1f',
             '0X1F', '-0x10', ' 1', '1 ', '+-1', '--1', '1x',
             '9223372036854775807', '9223372036854775808',
             '-9223372036854775808', '-9223372036854775809',
             '0x7fffffffffffffff', '0x8000000000000000',
             '-0x8000000000000000', '0777777777777777777777',
             '01000000000000000000000', '99999999999999999999',
             ' \x01 12 \x02'}
    while len(cases) < 3000:
        cases.add(''.join(random.choice(alphabet)
                          for _ in range(random.randint(0, 6))))
    return sorted(cases)


def build(build_dir, work):
    libs = ['shell-frontend', 'ansi-c', 'linking', 'goto-programs',
            'langapi', 'util', 'big-int', 'json', 'analyses',
            'pointer-analysis', 'assembler']
    gen_cpp = os.path.join(work, 'gen.cpp')
    with open(gen_cpp, 'w') as f:
        f.write(GENERATOR)
    gen = os.path.join(work, 'gen')
    subprocess.run(['g++', '-std=c++17', '-I', SRC, gen_cpp, '-o', gen,
                    '-L', os.path.join(build_dir, 'lib'),
                    '-Wl,--start-group'] + ['-l' + l for l in libs] +
                   ['-Wl,--end-group'], check=True)
    with open(os.path.join(work, 'runtime.c'), 'w') as f:
        f.write(subprocess.run([gen], check=True, capture_output=True,
                               text=True).stdout)
    harness_c = os.path.join(work, 'harness.c')
    with open(harness_c, 'w') as f:
        f.write(HARNESS)
    harness = os.path.join(work, 'harness')
    subprocess.run(['gcc', '-w', '-O1', '-o', harness, harness_c],
                   check=True)
    return harness


def compare(harness, cases, shell, any_space):
    ours = subprocess.run([harness, '1' if any_space else '0'],
                          input='\n'.join(cases) + '\n', capture_output=True,
                          text=True, check=True).stdout.splitlines()
    script = []
    for case, result in zip(cases, ours):
        test_value = result.split('|')[0].split()[1]
        quoted = "'" + decode(case).replace("'", "'\\''") + "'"
        script.append(
            f'x={quoted}; [ "$x" -eq {test_value} ] 2>/dev/null; '
            "printf '%s ' $?; (echo $((x))) 2>/dev/null || echo E")
    out = subprocess.run([shell], input='\n'.join(script) + '\n',
                         capture_output=True, text=True).stdout.splitlines()
    assert len(out) == len(cases)
    mismatches, flagged_but_accepted = [], 0
    for case, result, actual in zip(cases, ours, out):
        test, arith = result.split('|')
        status, value = actual.split(' ', 1)
        if (test[0] == 'T') != (status == '0') or \
           (test[0] == 'E' and status != '2'):
            mismatches.append(('test', repr(decode(case)), result, actual))
        if arith[0] == 'A' and value.strip() != arith.split()[1]:
            mismatches.append(('arith', repr(decode(case)), result, actual))
        if arith[0] == 'E' and value.strip() != 'E':
            flagged_but_accepted += 1
    return mismatches, flagged_but_accepted


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    build_dir = sys.argv[1]
    shells = [('dash', sys.argv[2] if len(sys.argv) > 2 else 'dash', True),
              ('bash', sys.argv[3] if len(sys.argv) > 3 else 'bash', False)]
    cases = make_cases()
    with tempfile.TemporaryDirectory() as work:
        harness = build(build_dir, work)
        failed = False
        for name, shell, any_space in shells:
            mismatches, flagged = compare(harness, cases, shell, any_space)
            print(f'{name}: {len(cases)} cases, {len(mismatches)} '
                  f'mismatches, {flagged} values accepted by {name} but '
                  'flagged by the model')
            for m in mismatches[:20]:
                print('  ', m)
            failed |= bool(mismatches)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
