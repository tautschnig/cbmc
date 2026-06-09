# Hardening the CBMC C front-end for the Linux kernel (full-TU goto-cc)

**Date:** 2026-06-09
**Goal:** let `goto-cc` compile real Linux kernel translation units (with
the real kernel headers) so CBMC can verify the *actual* compiled
functions — the basis for real-function harnessing (see
`real-function-harness-2026-06.md`).
**Method:** iterate — compile a parser TU with goto-cc (capture kbuild's
gcc command, swap `goto-cc`, strip `-Werror*`), fix the first front-end
gap, rebuild, re-survey, repeat.

## The fixes (each with a regression test)

| # | Commit | Gap | Kernel construct |
|---|--------|-----|------------------|
| 1 | 6e35acd7c1 | `__builtin_has_attribute` unparsed (2nd operand is a bare attribute name) → "failed to find symbol 'nonstring'" | fortify `strscpy` `_Static_assert` |
| 2 | 718e6b17e9 | `-fms-extensions` anonymous **tagged** struct/union members dropped (wrong size, members inaccessible) | `struct __filename_head;` in `struct filename` |
| 3 | 71658b0ac6 | `a ? : b` (omitted middle) not constant-folded in constant contexts | `__aligned((x+0) ? : SMP_CACHE_BYTES)` |
| 4 | ef863c2319 | `__builtin_strlen("literal")` not constant-folded | `module_param` `_Static_assert(... == __builtin_strlen(name))` |
| 5 | 1e987df6d2 | `_Generic` matched with `irept ==`, which ignores qualifier "comments" → `int*` matched `const int*` | `container_of_const()` / `inet_sk()` |
| 6 | 8ee30d3585 | `__attribute__((mode))` fallback made an enum's underlying type its own `c_enum_tag` (a cycle) → `alignment()` stack-overflow / `pointer_offset_bits` abort | enums with `mode(...)` in crypto/krb5 |
| 7 | ac43ec6c51 | a constant **compound literal** not treated as a compile-time constant | `DEFINE_RATELIMIT_STATE` / `pr_*_ratelimited` static init |
| 8 | 19d2ffae6e | `_Generic` controlling expression not array/function-decayed (C11 lvalue conversion) | `ATTRIBUTE_GROUPS` `_Generic(attrs[], struct attribute **: ...)` |

Plus one reusable build detail: the kernel passes `-Werror`, which
escalates goto-cc's (correct) "incompatible pointer types" *warning*
(e.g. `LIST_POISON`) to an error — strip `-Werror*` from the goto-cc
invocation.

Notably, the original assumption that "CBMC ignores unknown
`__attribute__`s" was **correct** — that was never the blocker (fix #1
was a missing *builtin*, not attribute handling).

## Coverage achieved

Full-TU goto-cc now builds (verified) across the kernel parser surface
and well beyond, e.g.:

* **net/**: sctp (`sm_make_chunk`, `sm_statefuns`), ipv4 (`ip_options`,
  `tcp_input`, `fib_trie`), ipv6 (`ip6_fib`, `exthdrs`), mptcp
  (`options`), netfilter (`xt_tcpmss`, `nf_tables_api`,
  `nft_set_pipapo`), wireless (`scan`, `nl80211`), ceph
  (`messenger_v2`), sched (`sch_netem`), bluetooth (`l2cap_core`,
  `hci_event`), rxrpc (`rxgk`, `rxkad`, `af_rxrpc`), tipc (`link`),
  9p (`protocol`), sunrpc (`xdr`), batman-adv, key/af_key,
  xfrm, core (`skbuff`, `filter`).
* **fs/**: ntfs3 (`run`), ext4 (`extents`), smb/server (`smb2pdu`).
* **drivers/**: hid (`hid-core`), usb/core (`config`), scsi
  (`scsi_lib`), net (`tun`, e1000), wireless (ath11k `dp_rx`),
  md (`dm-raid1`).

This is the bug-finding target (TLV / option / length / on-disk
parsers) plus much of the surrounding stack.

## Remaining (not front-end bugs, or narrow)

* **Build-config / include path**, not front-end issues: `xfs` needs
  `xfs_platform.h` on the include path; `f_ncm` needs `configfs.h`
  (`CONFIG_CONFIGFS_FS`); `net/dccp` not configured.  These are
  Kconfig/`-I` setup, not CBMC.
* **`kernel/bpf/verifier.c`**: `failed to find symbol
  'sk_filter_verifier_ops'` — taking the address of an `extern`
  (defined in another TU) in a file-scope array initializer. A narrow
  extern-resolution edge in a very large, non-parser TU; left as a
  documented gap (diminishing returns vs. the parser goal).

## How to use

```sh
# compile a kernel object with goto-cc, then verify a function in it:
CMD=$(make V=1 net/foo/bar.o 2>&1 | grep -E '^\s*gcc .*\.c\s*$' | tail -1)
GCMD=$(echo "$CMD" | sed "s#^\s*gcc #goto-cc #; s#-o [^ ]*\.o#-o bar.gb#; s/-Werror[=a-z-]*//g")
bash -c "$GCMD"
goto-cc -o h.gb harness.c            # harness calls bar() with a sized buffer
cbmc bar.gb h.gb --function harness --bounds-check --pointer-check --unwind N
```

All eight fixes are general CBMC C front-end improvements (not
kernel-specific hacks) and each ships with a `regression/ansi-c` test.
