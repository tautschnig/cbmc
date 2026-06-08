#!/usr/bin/env python3
"""Auto-harness generator for TLV/parse-loop candidates.

Input: a pipe-delimited candidate line from tlv_parse_loop.ql
  func|file|line|cursor=<c>|buf=<b>

Output: a self-contained CBMC harness (stdout) modelling the canonical
TLV walker the query matched:

  while (off < total) {            // bounded only by 'total'
      type = buf[off];             // header read  -- OOB if off   >= total
      len  = buf[off + 1];         // header read  -- OOB if off+1 >= total
      ... value bytes buf[off+2+i] // value read   -- OOB if off+2+len > total
      off += 2 + len;              // advance by in-band length
  }

The generated `harness_buggy` faithfully reproduces the unguarded
walk (no `off + 2 <= total` / `off + 2 + len <= total` check) so CBMC
finds the OOB; `harness_fixed` adds the checks and verifies clean.

Usage:
  tlv_parse_loop.ql results | while read l; do tlv_harness_gen.py -c "$l"; done
  goto-cc -o h.gb out.c && cbmc h.gb --function harness_buggy --bounds-check --unwind 10
"""
import sys
import argparse


def gen(func, filepath, line, cursor, buf):
    return f"""// Auto-generated TLV/parse-loop CBMC harness.
// Candidate: {func} ({filepath}:{line})  cursor={cursor} buf={buf}
//
// Models the matched walk: while ({cursor} < total) {{ t=buf[{cursor}];
// len=buf[{cursor}+1]; ...; {cursor} += 2 + len; }}.  buggy -> FAILED
// (pointer/array OOB), fixed -> SUCCESSFUL.
//   goto-cc -o h.gb <this> && cbmc h.gb --function harness_buggy \\
//       --bounds-check --pointer-check --unwind 10
#include <stdint.h>

#define BUFMAX 8 // scaled fixed input buffer (bounded model)

uint16_t nd_u16(void)
{{
  uint16_t x;
  return x;
}}

// Faithful to the candidate: bounded only by `total`, no header/value
// fits check.  `offset` widths follow the common kernel shape (u8/u16).
int harness_buggy(void)
{{
  uint8_t buf[BUFMAX];
  uint16_t total = nd_u16();
  __CPROVER_assume(total <= BUFMAX);

  uint16_t off = 0;
  uint32_t acc = 0;
  while (off < total) {{
    uint8_t type = buf[off];     // OOB: off may be total-? near end
    uint8_t len = buf[off + 1];  // OOB: off+1 may be >= total
    uint16_t i = 0;
    while (i < len) {{
      acc += buf[off + 2 + i];   // OOB: off+2+len may exceed total/BUFMAX
      i++;
    }}
    off += 2 + len;
  }}
  return (int)acc;
}}

int harness_fixed(void)
{{
  uint8_t buf[BUFMAX];
  uint16_t total = nd_u16();
  __CPROVER_assume(total <= BUFMAX);

  uint16_t off = 0;
  uint32_t acc = 0;
  while (off + 2 <= total) {{        // header must fit
    uint8_t len = buf[off + 1];
    if (off + 2 + (uint16_t)len > total) // value must fit
      break;
    uint16_t i = 0;
    while (i < len) {{
      acc += buf[off + 2 + i];
      i++;
    }}
    off += 2 + len;
  }}
  return (int)acc;
}}
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidate", "-c", required=True)
    a = ap.parse_args()
    parts = a.candidate.split("|")
    if len(parts) < 5:
        sys.exit(f"expected func|file|line|cursor=..|buf=.., got: {a.candidate}")
    func, filepath, line = parts[0], parts[1], parts[2]
    cursor = parts[3].split("=", 1)[-1]
    buf = parts[4].split("=", 1)[-1]
    print(gen(func, filepath, line, cursor, buf))


if __name__ == "__main__":
    main()
