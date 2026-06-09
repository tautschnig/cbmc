#!/usr/bin/env python3
"""Auto-harness generator for the Gap #1/#2/#3 oracles.

Emits a self-contained CBMC harness (stdout) modelling the bug shape the
oracle matched, parameterised by the candidate's extracted constants.
Each harness exposes `harness_buggy` (-> VERIFICATION FAILED) and
`harness_fixed` (-> SUCCESSFUL), mirroring the hand-written templates
loop_oracle_variants.c and decoded_len_overflow.c.

Candidate line formats (pipe-delimited, as produced by each .ql):
  count    : func|file|line|count-loop-write|count=<v>|arr=<name>[<size>]
             func|file|line|direct-index|index=<v>|arr=<name>[<size>]
  decoded  : func|file|line|decoded len '<v>' -> round_up()|xdr_round_up()|multiply
  skb      : func|file|line|skb->data read, no length guard in function

Recommended CBMC flags by oracle (the driver applies these):
  count : --bounds-check --pointer-check
  decoded: --unsigned-overflow-check
  skb   : --bounds-check --pointer-check

Usage:
  oracle_harness_gen.py --oracle count   -c "<line>"
  oracle_harness_gen.py --oracle decoded -c "<line>"
  oracle_harness_gen.py --oracle skb     -c "<line>"
"""
import sys
import argparse
import re


def _arr_size(detail, default=8):
    m = re.search(r"\[(\d+)\]", detail)
    return int(m.group(1)) if m else default


def gen_count(func, filepath, line, kind, detail):
    size = _arr_size(detail)
    # cap the modelled array so the bounded check stays small but faithful
    msize = size if size <= 6 else 6
    if kind == "count-loop-write":
        return f"""// Auto-generated count/index CBMC harness (Gap #1, count-loop-write).
// Candidate: {func} ({filepath}:{line})  {detail}
// Shape: for (i=0; i<count; i++) dest[i]=...  with fixed dest[{size}] and
// no `count <= ARRAY_SIZE` guard.  buggy -> FAILED (write OOB), fixed ok.
//   goto-cc -o h.gb <this> && cbmc h.gb --function harness_buggy \\
//       --bounds-check --pointer-check --unwind {msize + 2}
#include <stdint.h>
#include "cover_probe.h"
#define DEST {msize}
unsigned nd(void) {{ unsigned x; return x; }}

int harness_buggy(void)
{{
  int dest[DEST];
  unsigned count = nd();          // attacker/firmware-controlled count
  CHECKPOINT(oob, count > DEST);  // OOB-write precondition reachable?
  for (unsigned i = 0; i < count; i++)
    dest[i] = (int)i;             // OOB write when count > DEST
  return dest[0];
}}

int harness_fixed(void)
{{
  int dest[DEST];
  unsigned count = nd();
  if (count > DEST)               // FIX: bound count against ARRAY_SIZE
    return -1;
  CHECKPOINT(oob, count > DEST);  // after guard: BLOCKED
  for (unsigned i = 0; i < count; i++)
    dest[i] = (int)i;
  return dest[0];
}}
"""
    else:  # direct-index
        return f"""// Auto-generated count/index CBMC harness (Gap #1, direct-index).
// Candidate: {func} ({filepath}:{line})  {detail}
// Shape: dest[idx] with fixed dest[{size}] and no `idx < ARRAY_SIZE` guard.
//   goto-cc -o h.gb <this> && cbmc h.gb --function harness_buggy \\
//       --bounds-check --pointer-check --unwind 4
#include <stdint.h>
#include "cover_probe.h"
#define DEST {msize}
unsigned nd(void) {{ unsigned x; return x; }}

int harness_buggy(void)
{{
  int dest[DEST];
  unsigned idx = nd();            // attacker/firmware-controlled index
  CHECKPOINT(oob, idx >= DEST);   // OOB precondition reachable?
  return dest[idx];               // OOB when idx >= DEST
}}

int harness_fixed(void)
{{
  int dest[DEST];
  unsigned idx = nd();
  if (idx >= DEST)                // FIX: bound the index
    return -1;
  CHECKPOINT(oob, idx >= DEST);   // after guard: BLOCKED
  return dest[idx];
}}
"""


def gen_decoded(func, filepath, line, detail):
    is_mul = "multiply" in detail
    if is_mul:
        body = """  uint32_t n = nd();              // length/count decoded from the wire
  CHECKPOINT(oob, n > 0xffffffffu / 16u); // overflow precondition reachable?
  return n * 16u;                 // BUG: multiplication overflows"""
        fixedbody = """  uint32_t n = nd();
  if (n > 0xffffffffu / 16u)      // FIX: bound so product cannot overflow
    return 0;
  CHECKPOINT(oob, n > 0xffffffffu / 16u); // after guard: BLOCKED
  return n * 16u;"""
        flag = "multiply"
    else:
        body = """  uint32_t len = nd();            // length decoded from the wire
  CHECKPOINT(oob, len > 0xfffffffbu); // overflow precondition reachable?
  return (len + 3u) & ~3u;        // BUG: round-up overflows near UINT_MAX"""
        fixedbody = """  uint32_t len = nd();
  if (len > 0xfffffffbu)          // FIX: UINT_MAX-3, round-up cannot overflow
    return 0;
  CHECKPOINT(oob, len > 0xfffffffbu); // after guard: BLOCKED
  return (len + 3u) & ~3u;"""
        flag = "round-up"
    return f"""// Auto-generated decoded-length CBMC harness (Gap #3, {flag}).
// Candidate: {func} ({filepath}:{line})  {detail}
// Shape: a length decoded from the wire fed into {flag} arithmetic that
// integer-overflows (rxgk CVE-2026-31633 class).  buggy -> FAILED
// (arithmetic overflow), fixed -> SUCCESSFUL.
//   goto-cc -o h.gb <this> && cbmc h.gb --function harness_buggy \\
//       --unsigned-overflow-check --unwind 4
#include <stdint.h>
#include "cover_probe.h"
uint32_t nd(void) {{ uint32_t x; return x; }}

uint32_t harness_buggy(void)
{{
{body}
}}

uint32_t harness_fixed(void)
{{
{fixedbody}
}}
"""


def gen_skb(func, filepath, line, detail):
    return f"""// Auto-generated skb-field CBMC harness (Gap #2).
// Candidate: {func} ({filepath}:{line})  {detail}
// Shape: read a 2-byte field at an offset from a received buffer of
// nondeterministic length, with no `len >= offset+2` (pskb_may_pull)
// guard.  The buffer is modelled as exactly `len` valid bytes (as
// skb->data is), so reading at offset 2..3 when len < 4 is a real OOB.
// buggy -> FAILED (OOB read), fixed -> SUCCESSFUL.
//   goto-cc -o h.gb <this> && cbmc h.gb --function harness_buggy \\
//       --bounds-check --pointer-check --unwind 4
#include <stdint.h>
#include <stdlib.h>
#include "cover_probe.h"
unsigned nd(void) {{ unsigned x; return x; }}

// read a u16 field at byte offset 2 (e.g. an SDU/length field)
int harness_buggy(void)
{{
  unsigned len = nd();
  __CPROVER_assume(len >= 1 && len <= 8);
  uint8_t *data = malloc(len);    // exactly `len` valid bytes, like skb->data
  __CPROVER_assume(data != 0);
  // BUG: no check that len >= 4 before reading the field at offset 2..3
  CHECKPOINT(oob, len < 4);        // OOB-read precondition reachable?
  return data[2] | (data[3] << 8); // OOB read when len < 4
}}

int harness_fixed(void)
{{
  unsigned len = nd();
  __CPROVER_assume(len >= 1 && len <= 8);
  uint8_t *data = malloc(len);
  __CPROVER_assume(data != 0);
  if (len < 4)                     // FIX: pskb_may_pull equivalent
    return -1;
  CHECKPOINT(oob, len < 4);        // after guard: BLOCKED
  return data[2] | (data[3] << 8);
}}
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oracle", required=True, choices=["count", "decoded", "skb"])
    ap.add_argument("--candidate", "-c", required=True)
    a = ap.parse_args()
    p = a.candidate.split("|")
    if len(p) < 4:
        sys.exit(f"expected func|file|line|..., got: {a.candidate}")
    func, filepath, line = p[0], p[1], p[2]
    if a.oracle == "count":
        # func|file|line|kind|detail...(rest joined)
        kind = p[3]
        detail = "|".join(p[4:])
        print(gen_count(func, filepath, line, kind, detail))
    elif a.oracle == "decoded":
        detail = "|".join(p[3:])
        print(gen_decoded(func, filepath, line, detail))
    else:
        detail = "|".join(p[3:])
        print(gen_skb(func, filepath, line, detail))


if __name__ == "__main__":
    main()
