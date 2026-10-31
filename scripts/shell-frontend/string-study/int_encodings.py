#!/usr/bin/env python3
"""Compare SMT-LIB encodings of shell integer <-> string conversions.

Shell arithmetic is 64-bit, but SMT-LIB strings convert to and from Int
(str.to_int, str.from_int). This writes one query per encoding for the
script

    x=$1
    if [ "$x" -gt 5 ]; then y=$((x + 1)); assert "$y" != K; fi

where K = 3 gives an unsatisfiable query (the assertion holds) and K = 7 a
satisfiable one (x = 6), and times z3 and cvc5 on each.

Encodings:
  mixed   64-bit arithmetic; int2bv (str.to_int d) and str.from_int (bv2nat v)
  int     arithmetic in Int, with range checks for 64-bit overflow
  digits  64-bit arithmetic; strings converted digit by digit with str.at
          and ite over the ten digit strings, so there is no Int-BV mixing

Usage: int_encodings.py [OUTDIR]
"""

import os
import subprocess
import sys
import time

HEADER = '(set-logic ALL)\n'
DIGITS_RE = '(re.+ (re.range "0" "9"))'
MAX_DIGITS = 19


def parse_mixed(d):
    return f'((_ int2bv 64) (str.to_int {d}))'


def format_mixed(v):
    return f'(str.from_int (bv2nat {v}))'


def digit_bv(c):
    """The value of the one-character string c as an 8-bit vector."""
    out = '(_ bv0 64)'
    for k in range(9, 0, -1):
        out = f'(ite (= {c} "{k}") (_ bv{k} 64) {out})'
    return out


def parse_digits(d):
    """Horner's rule over at most MAX_DIGITS characters of d."""
    acc = '(_ bv0 64)'
    for k in range(MAX_DIGITS):
        acc = (f'(ite (< {k} (str.len {d})) (bvadd (bvmul {acc} (_ bv10 64)) '
               f'{digit_bv(f"(str.at {d} {k})")}) {acc})')
    return acc


def digit_str(b):
    out = '"9"'
    for k in range(8, -1, -1):
        out = f'(ite (= {b} (_ bv{k} 64)) "{k}" {out})'
    return out


def format_digits(v):
    """Decimal digits of a non-negative 64-bit value."""
    parts = []
    for k in range(MAX_DIGITS, -1, -1):
        p = 10 ** k
        digit = f'(bvurem (bvudiv {v} (_ bv{p} 64)) (_ bv10 64))'
        if k == 0:
            parts.append(digit_str(digit))
        else:
            parts.append(
                f'(ite (bvuge {v} (_ bv{p} 64)) {digit_str(digit)} "")')
    return '(str.++ ' + ' '.join(parts) + ')'


def query(encoding, k):
    q = HEADER + '(declare-fun x () String)\n'
    # x is a valid integer of at most 19 digits, so the conversions agree
    q += f'(assert (str.in_re x {DIGITS_RE}))\n'
    q += f'(assert (<= (str.len x) {MAX_DIGITS}))\n'
    if encoding == 'int':
        q += '(define-fun n () Int (str.to_int x))\n'
        q += '(assert (<= n 9223372036854775807))\n'
        q += '(assert (> n 5))\n'
        q += '(define-fun m () Int (+ n 1))\n'
        q += '(assert (<= m 9223372036854775807))\n'
        q += '(define-fun y () String (str.from_int m))\n'
    else:
        parse = parse_mixed if encoding == 'mixed' else parse_digits
        fmt = format_mixed if encoding == 'mixed' else format_digits
        q += f'(define-fun n () (_ BitVec 64) {parse("x")})\n'
        if encoding == 'mixed':
            q += '(assert (<= (str.to_int x) 9223372036854775807))\n'
        else:
            q += '(assert (bvsge n (_ bv0 64)))\n'
        q += '(assert (bvsgt n (_ bv5 64)))\n'
        q += '(define-fun m () (_ BitVec 64) (bvadd n (_ bv1 64)))\n'
        q += '(assert (bvsgt m n))\n'
        q += f'(define-fun y () String {fmt("m")})\n'
    q += f'(assert (= y "{k}"))\n(check-sat)\n'
    return q


def run(solver, path):
    command = {'z3': ['z3', path],
               'cvc5': ['cvc5', '--lang', 'smt2', path]}[solver]
    start = time.time()
    try:
        r = subprocess.run(command, capture_output=True, text=True,
                           timeout=60)
        out = r.stdout.strip().split('\n')[0] if r.stdout else r.stderr[:40]
    except subprocess.TimeoutExpired:
        out = 'timeout'
    return out, time.time() - start


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else '/tmp/int-encodings'
    os.makedirs(outdir, exist_ok=True)
    print(f'{"encoding":8} {"K":>2} {"expected":9} {"z3":>16} {"cvc5":>16}')
    for encoding in ('mixed', 'int', 'digits'):
        for k, expected in ((3, 'unsat'), (7, 'sat')):
            path = os.path.join(outdir, f'{encoding}-{k}.smt2')
            with open(path, 'w') as f:
                f.write(query(encoding, k))
            cells = []
            for solver in ('z3', 'cvc5'):
                out, sec = run(solver, path)
                mark = '' if out == expected else '!'
                cells.append(f'{out}{mark} {sec:.2f}s')
            print(f'{encoding:8} {k:2} {expected:9} {cells[0]:>16} '
                  f'{cells[1]:>16}')


if __name__ == '__main__':
    main()
