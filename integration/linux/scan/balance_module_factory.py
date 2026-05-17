#!/usr/bin/env python3
"""
balance_module_factory.py - generate a refcount-balance property module
from a small per-module config.

The cred_lifetime / refcount_lifetime / kobject_lifetime modules share
~80% of their code: a per-pointer ghost refcount, a get/put that
adjusts the ghost, a `<type>_live(p)` predicate (ghost > 0), and a
contract on the kernel's put-API that requires `<type>_live(p) == 1`.
This factory produces all nine files (header, reference impl, cocci
prefilter, unit test, regression driver, README, kernel adapter,
adapter probe, direct-call harness) from a single config dict, and
prints the snippets that have to be pasted into

  - integration/linux/scan/synthesise_harness.py     (MODULE_GHOST_BOOTSTRAP)
  - integration/linux/scan/scan.py                  (CONTRACT_FUNCTIONS,
                                                     KERNEL_ADAPTERS,
                                                     _PER_FILE_SUPPORTED_MODULES)
  - integration/linux/scan/scan-per-file.sh         (ADAPTER_STEM,
                                                     CONTRACT_TARGETS)

Why not auto-inject?  The wiring sites are not at predictable
markers; auto-injection is fragile.  Printing the snippets keeps the
factory simple and lets a human review each insertion.

Usage:
  ./balance_module_factory.py <config.json>

  Or, more commonly, edit the BUILTIN_CONFIGS dict at the bottom of
  this file and run:

  ./balance_module_factory.py --module sock_lifetime

A config:

  {
    "module":            "sock_lifetime",      # property module dir name
    "type_text":         "struct sock",        # base type as written
    "param_name":        "sk",                 # parameter name in the
                                               # kernel put-API decl
    "get_fn":            "sock_hold",
    "put_fn":            "sock_put",
    "kernel_header":     "<net/sock.h>",
    "is_static_inline":  true,                 # sock_put is inline
    "static_inline_in":  "sock",               # for mangling, header
                                               # stem (sock.h -> "sock")
    "adapter_short":     "sock",               # adapter file stem
    "ghost_short":       "sock",               # type prefix for ghost API
    "cve_motivation":    "AF_VSOCK / vsock_sk_destruct UAF (CVE-2024-...)",
    "subsystem_focus":   "net/*",
    "smoke_target":      "net/socket.c"
  }

The factory writes to:

  integration/linux/properties/<module>/{<module>.{h,c,cocci},
                                         test_unit.c, run.sh, README.md}
  integration/linux/scan/adapters/<adapter_short>_kernel_adapter{,_probe}.c
  integration/linux/scan/adapters/<adapter_short>_kernel_direct_harness.c
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path

import jinja2


REPO_ROOT = Path(__file__).resolve().parents[3]
PROPERTIES_DIR = REPO_ROOT / "integration" / "linux" / "properties"
ADAPTERS_DIR = REPO_ROOT / "integration" / "linux" / "scan" / "adapters"


@dataclass
class ModuleConfig:
    module: str               # e.g. "sock_lifetime"
    type_text: str            # e.g. "struct sock"
    param_name: str           # e.g. "sk"
    get_fn: str               # e.g. "sock_hold"
    put_fn: str               # e.g. "sock_put"
    kernel_header: str        # e.g. "<net/sock.h>"
    is_static_inline: bool    # whether put_fn is static inline
    static_inline_in: str     # header stem for mangling (no .h)
    adapter_short: str        # adapter file stem (e.g. "sock")
    ghost_short: str          # type prefix for ghost API (e.g. "sock")
    cve_motivation: str
    subsystem_focus: str
    smoke_target: str
    # Optional: additional parameters on the put-API beyond the
    # primary `<type> *<param_name>` argument.  E.g. kref_put has a
    # `void (*release)(struct kref *kref)` second argument.  Each
    # tuple is (type_text, param_name).  Empty for plain
    # balance modules.
    extra_params: list[tuple[str, str]] = field(default_factory=list)
    # Optional: return type of put_fn.  Default "void".  E.g.
    # kref_put returns int (1 if usage went to 0, 0 otherwise).
    return_type: str = "void"

    @property
    def type_param_text(self) -> str:
        return f"{self.type_text} *"

    @property
    def upper(self) -> str:
        return self.module.upper()

    @property
    def ghost_init_fn(self) -> str:
        return f"{self.module}_init"

    @property
    def ghost_get_fn(self) -> str:
        return f"{self.module}_get"

    @property
    def ghost_put_fn(self) -> str:
        return f"{self.module}_put"

    @property
    def ghost_usage_fn(self) -> str:
        return f"{self.module}_usage"

    @property
    def live_predicate(self) -> str:
        # Convention: <ghost_short>_live  (e.g. sock_live, dentry_live)
        return f"{self.ghost_short}_live"

    @property
    def mangled_put(self) -> str:
        if not self.is_static_inline:
            return self.put_fn
        return f"__CPROVER_file_local_{self.static_inline_in}_h_{self.put_fn}"

    @property
    def put_fn_full_params(self) -> str:
        """Full parameter list for put_fn declarations:
        primary param + any extras, comma-separated.

        Function-pointer types like
        `void (*release)(struct kref *kref)` already contain the
        parameter name inside the inner parens, so we don't add a
        separate name suffix for them — that would produce
        `void (*release)(...) release` which is a syntax error."""
        primary = f"{self.type_param_text}{self.param_name}"
        if not self.extra_params:
            return primary
        extras: list[str] = []
        for t, n in self.extra_params:
            if "(*" in t and ")" in t:
                # Function-pointer type — name is already in t.
                extras.append(t)
            else:
                extras.append(f"{t} {n}")
        return primary + ", " + ", ".join(extras)

    @property
    def put_fn_extra_args(self) -> str:
        """Comma-prefixed list of placeholder arg values for the
        extra params, used in the harness's call to put_fn.  For
        function-pointer params we synthesise a dummy stub.  For
        plain pointers we pass NULL.  For integers we pass 0."""
        if not self.extra_params:
            return ""
        out = []
        for t, n in self.extra_params:
            if "(*" in t and ")" in t:
                # function pointer — supply the harness stub
                out.append("__harness_release_stub")
            elif "*" in t:
                out.append(f"({t.strip()})0")
            else:
                out.append("0")
        return ", " + ", ".join(out)

    @property
    def harness_extra_decls(self) -> str:
        """Any auxiliary declarations the harness needs because of
        extra params — currently just a release stub for kref-
        style function-pointer args."""
        for t, n in self.extra_params:
            if "(*" in t and ")" in t:
                # function-pointer arg: emit a no-op stub.
                # The pointer signature is the type with the inner
                # name replaced.
                return (
                    "static void __harness_release_stub("
                    f"{self.type_param_text}{self.param_name})\n"
                    f"{{\n"
                    f"  (void){self.param_name};\n"
                    f"}}\n"
                )
        return ""

    @property
    def harness_call_lhs(self) -> str:
        """LHS prefix for put_fn call when return type is non-void.
        Empty for void."""
        if self.return_type == "void":
            return ""
        return f"({self.return_type})"


# ---------------------------------------------------------------------
# Jinja2 templates.
# Heredocs use raw strings with custom delimiters that don't clash
# with C / shell.
# ---------------------------------------------------------------------

ENV = jinja2.Environment(
    block_start_string="<%",
    block_end_string="%>",
    variable_start_string="<{",
    variable_end_string="}>",
    comment_start_string="<#",
    comment_end_string="#>",
    keep_trailing_newline=True,
)


HEADER_T = ENV.from_string(r"""
/// \file
/// <{ cfg.module }>.h — property module for Linux kernel
/// `<{ cfg.type_text }>` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `<{ cfg.type_text }>` lifetimes through a
/// reference count: `<{ cfg.get_fn }>(<{ cfg.param_name }>)`
/// increments it; `<{ cfg.put_fn }>(<{ cfg.param_name }>)`
/// decrements and, when the count reaches zero, releases the
/// object.  Two related bug shapes recur:
///
///   1. **Double put.**  An error path puts an object that an
///      earlier success path already put, dropping the refcount
///      below the live threshold and freeing memory some other
///      code path still holds.
///
///   2. **Put without a matching get.**  Code obtains a reference
///      by other means (sysfs, container_of on a list pointer)
///      and then puts it.  Same UAF outcome.
///
/// Motivating CVE family: <{ cfg.cve_motivation }>.
/// Subsystem focus: `<{ cfg.subsystem_focus }>`.
///
/// ## Abstraction
///
/// `<{ cfg.type_text }>` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_<{ cfg.upper }>_<{ cfg.upper }>_H
#define INTEGRATION_LINUX_PROPERTIES_<{ cfg.upper }>_<{ cfg.upper }>_H

#include <stddef.h>

// `<{ cfg.type_text }>` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
<{ cfg.type_text }>;

// Ghost state API.
void <{ cfg.ghost_init_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>,
                              unsigned int usage);
void <{ cfg.ghost_get_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>);
void <{ cfg.ghost_put_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>);
unsigned int <{ cfg.ghost_usage_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int <{ cfg.live_predicate }>(<{ cfg.type_param_text }><{ cfg.param_name }>);

#endif
""")


REF_IMPL_T = ENV.from_string(r"""
/// \file
/// <{ cfg.module }>.c — reference implementation of the
/// <{ cfg.module }> property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "<{ cfg.module }>.h"

#ifndef <{ cfg.upper }>_GHOST_TABLE_SIZE
#  define <{ cfg.upper }>_GHOST_TABLE_SIZE 16
#endif

struct <{ cfg.ghost_short }>_ghost_entry
{
  <{ cfg.type_param_text }>key;
  unsigned int usage;
};

static struct <{ cfg.ghost_short }>_ghost_entry
  <{ cfg.ghost_short }>_ghost_table[<{ cfg.upper }>_GHOST_TABLE_SIZE];
static unsigned int <{ cfg.ghost_short }>_ghost_table_len = 0;

static struct <{ cfg.ghost_short }>_ghost_entry *
<{ cfg.ghost_short }>_ghost_find(<{ cfg.type_param_text }><{ cfg.param_name }>)
{
  for(unsigned int i = 0; i < <{ cfg.ghost_short }>_ghost_table_len; i++)
  {
    if(<{ cfg.ghost_short }>_ghost_table[i].key == <{ cfg.param_name }>)
      return &<{ cfg.ghost_short }>_ghost_table[i];
  }
  return (struct <{ cfg.ghost_short }>_ghost_entry *)0;
}

static struct <{ cfg.ghost_short }>_ghost_entry *
<{ cfg.ghost_short }>_ghost_find_or_add(<{ cfg.type_param_text }><{ cfg.param_name }>)
{
  struct <{ cfg.ghost_short }>_ghost_entry *existing =
    <{ cfg.ghost_short }>_ghost_find(<{ cfg.param_name }>);
  if(existing)
    return existing;
  if(<{ cfg.ghost_short }>_ghost_table_len >= <{ cfg.upper }>_GHOST_TABLE_SIZE)
    return (struct <{ cfg.ghost_short }>_ghost_entry *)0;
  struct <{ cfg.ghost_short }>_ghost_entry *slot =
    &<{ cfg.ghost_short }>_ghost_table[<{ cfg.ghost_short }>_ghost_table_len++];
  slot->key = <{ cfg.param_name }>;
  slot->usage = 0;
  return slot;
}

void <{ cfg.ghost_init_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>,
                              unsigned int usage)
{
  struct <{ cfg.ghost_short }>_ghost_entry *slot =
    <{ cfg.ghost_short }>_ghost_find_or_add(<{ cfg.param_name }>);
  if(slot)
    slot->usage = usage;
}

void <{ cfg.ghost_get_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>)
{
  struct <{ cfg.ghost_short }>_ghost_entry *slot =
    <{ cfg.ghost_short }>_ghost_find_or_add(<{ cfg.param_name }>);
  if(slot)
    slot->usage++;
}

void <{ cfg.ghost_put_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>)
{
  struct <{ cfg.ghost_short }>_ghost_entry *slot =
    <{ cfg.ghost_short }>_ghost_find(<{ cfg.param_name }>);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int <{ cfg.ghost_usage_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>)
{
  struct <{ cfg.ghost_short }>_ghost_entry *slot =
    <{ cfg.ghost_short }>_ghost_find(<{ cfg.param_name }>);
  if(!slot)
    return 0;
  return slot->usage;
}

int <{ cfg.live_predicate }>(<{ cfg.type_param_text }><{ cfg.param_name }>)
{
  if(!<{ cfg.param_name }>)
    return 0;
  return <{ cfg.ghost_usage_fn }>(<{ cfg.param_name }>) > 0 ? 1 : 0;
}
""")


COCCI_T = ENV.from_string(r"""
// @@
//   SmPL rule: <{ cfg.module }>.cocci
//
//   Coccinelle prefilter for the <{ cfg.module }> property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `<{ cfg.put_fn }>`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `<{ cfg.put_fn }>(x) ...
//      <{ cfg.put_fn }>(x)` with no intervening `<{ cfg.get_fn }>(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: <{ cfg.cve_motivation }>.
// @@

@ <{ cfg.put_fn }>_call @
expression x;
position p;
@@

<{ cfg.put_fn }>@p(x);

@ script:python <{ cfg.put_fn }>_report @
p << <{ cfg.put_fn }>_call.p;
@@

coccilib.report.print_report(p[0],
    "<{ cfg.module }>: <{ cfg.put_fn }> call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "<{ cfg.type_text }>)")

@ back_to_back_<{ cfg.put_fn }> @
expression x;
position p;
@@

<{ cfg.put_fn }>(x);
... when != <{ cfg.get_fn }>(x)
    when != \(x = \( <{ cfg.get_fn }>(...) \| ... \)\)
<{ cfg.put_fn }>@p(x);

@ script:python back_to_back_<{ cfg.put_fn }>_report @
p << back_to_back_<{ cfg.put_fn }>.p;
@@

coccilib.report.print_report(p[0],
    "<{ cfg.module }>: BUG-SHAPE: back-to-back <{ cfg.put_fn }>(x) "
    "... <{ cfg.put_fn }>(x) without intervening "
    "<{ cfg.get_fn }>(x) (double-put / unbalanced put)")
""")


TEST_UNIT_T = ENV.from_string(r"""
/// \file
/// test_unit.c — unit tests for the <{ cfg.module }> property
/// module.

#include "<{ cfg.module }>.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

// Concrete `<{ cfg.type_text }>` for this abstract test.  The
// property module's public header forward-declares it; any TU
// that wants to stack-allocate a sentinel provides its own
// layout.  The field doesn't matter — the ghost table only
// uses pointer identity.
<{ cfg.type_text }>
{
  int dummy;
};

int main(void)
{
  <{ cfg.type_text }> a, b, c;

  // test 1: fresh un-init -> not live (ghost defaults to 0).
  __CPROVER_assert(
    <{ cfg.live_predicate }>(&a) == 0,
    "untracked reports not live");

  // test 2: init with usage=1 -> live; put once -> not live.
  <{ cfg.ghost_init_fn }>(&b, 1);
  __CPROVER_assert(<{ cfg.live_predicate }>(&b) == 1, "usage=1 is live");
  <{ cfg.ghost_put_fn }>(&b);
  __CPROVER_assert(
    <{ cfg.live_predicate }>(&b) == 0, "after single put, not live");

  // test 3: init with usage=2 -> live; get -> live; put thrice
  //         brings usage from 3 down to 0.
  <{ cfg.ghost_init_fn }>(&c, 2);
  <{ cfg.ghost_get_fn }>(&c);
  __CPROVER_assert(<{ cfg.live_predicate }>(&c) == 1, "usage=3 still live");
  <{ cfg.ghost_put_fn }>(&c);
  __CPROVER_assert(<{ cfg.live_predicate }>(&c) == 1, "usage=2 still live");
  <{ cfg.ghost_put_fn }>(&c);
  __CPROVER_assert(<{ cfg.live_predicate }>(&c) == 1, "usage=1 still live");
  <{ cfg.ghost_put_fn }>(&c);
  __CPROVER_assert(<{ cfg.live_predicate }>(&c) == 0, "usage=0 is dead");

  // test 4: NULL -> not live (defensive).
  __CPROVER_assert(
    <{ cfg.live_predicate }>((<{ cfg.type_param_text }>)0) == 0,
    "NULL is not live");

  return 0;
}
""")


RUN_SH_T = ENV.from_string(r"""
#!/usr/bin/env bash
#
# Regression driver for the <{ cfg.module }> property module.
#
# One test: unit exercise of the ghost store + <{ cfg.live_predicate }>
# predicate.  Expect VERIFICATION SUCCESSFUL.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: <{ cfg.module }> ghost + <{ cfg.live_predicate }> predicate ==="
run_gotocc <{ cfg.module }>.c test_unit.c -o "$tmp/unit.gb"
out=$(run_cbmc "$tmp/unit.gb" --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
  echo "  [ok] unit: VERIFICATION SUCCESSFUL"
else
  echo "  [FAIL] unit: did not see VERIFICATION SUCCESSFUL" >&2
  echo "$out" | tail -20 | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "<{ cfg.module }> tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
""")


README_T = ENV.from_string(r"""
# <{ cfg.module }> property module

Tracks the Linux kernel `<{ cfg.type_text }>` refcount to catch two
related bug patterns:

1. **Double put.**  `<{ cfg.put_fn }>(<{ cfg.param_name }>)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `<{ cfg.type_text }>` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `<{ cfg.type_text }>`'s notional
refcount; `<{ cfg.get_fn }>` increments and `<{ cfg.put_fn }>`
decrements; the `<{ cfg.live_predicate }>(<{ cfg.param_name }>)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `<{ cfg.put_fn }>`.

Motivating CVE class: <{ cfg.cve_motivation }>.
Subsystem focus: `<{ cfg.subsystem_focus }>`.

## Files

- [`<{ cfg.module }>.h`](<{ cfg.module }>.h) — public API.
- [`<{ cfg.module }>.c`](<{ cfg.module }>.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`<{ cfg.module }>.cocci`](<{ cfg.module }>.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/<{ cfg.adapter_short }>_kernel_adapter.c`](../../scan/adapters/<{ cfg.adapter_short }>_kernel_adapter.c)
  attaches `__CPROVER_requires(<{ cfg.live_predicate }>(<{ cfg.param_name }>) == 1)`
  to `<{ cfg.put_fn }>`.<% if cfg.is_static_inline %>  Because `<{ cfg.put_fn }>` is
  `static inline` in `<{ cfg.kernel_header }>` the adapter
  attaches the contract to both the unmangled name and the
  goto-cc `--export-file-local-symbols` mangled form
  `<{ cfg.mangled_put }>` so it applies in every kernel TU that
  includes the header.<% endif %>
- Direct-call harness:
  [`../../scan/adapters/<{ cfg.adapter_short }>_kernel_direct_harness.c`](../../scan/adapters/<{ cfg.adapter_short }>_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `<{ cfg.type_param_text }>` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
""")


ADAPTER_T = ENV.from_string(r"""
/// \file
/// <{ cfg.adapter_short }>_kernel_adapter.c — attaches the
/// <{ cfg.module }> property module's `<{ cfg.live_predicate }>`
/// predicate as a contract precondition on the kernel's
/// `<{ cfg.put_fn }>` API.

<{ cfg.type_text }>;

int <{ cfg.live_predicate }>(<{ cfg.type_param_text }><{ cfg.param_name }>);
<% if cfg.is_static_inline %>
// Contract on the mangled static-inline form.  Parameter name
// `<{ cfg.param_name }>` must match the kernel's
// `<{ cfg.kernel_header }>` declaration; mismatch triggers an
// invariant violation in goto-instrument
// --replace-call-with-contract at contract-installation time.
<{ cfg.return_type }> <{ cfg.mangled_put }>(<{ cfg.put_fn_full_params }>)
  __CPROVER_requires(<{ cfg.param_name }> != (<{ cfg.type_param_text }>)0)
  __CPROVER_requires(<{ cfg.live_predicate }>(<{ cfg.param_name }>) == 1)
  __CPROVER_assigns();

<% endif %>
// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
<{ cfg.return_type }> <{ cfg.put_fn }>(<{ cfg.put_fn_full_params }>)
  __CPROVER_requires(<{ cfg.param_name }> != (<{ cfg.type_param_text }>)0)
  __CPROVER_requires(<{ cfg.live_predicate }>(<{ cfg.param_name }>) == 1)
  __CPROVER_assigns();
""")


ADAPTER_PROBE_T = ENV.from_string(r"""
/// \file
/// <{ cfg.adapter_short }>_kernel_adapter_probe.c — vacuity-probe
/// variant of <{ cfg.adapter_short }>_kernel_adapter.c.  The
/// substantive precondition is replaced with
/// `__CPROVER_requires(0 == 1)`.  scan.py links this in place of
/// the real adapter for a one-shot probe run that MUST fail,
/// proving the contract call site is reachable.

<{ cfg.type_text }>;
<% if cfg.is_static_inline %>
<{ cfg.return_type }> <{ cfg.mangled_put }>(<{ cfg.put_fn_full_params }>)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();

<% endif %>
<{ cfg.return_type }> <{ cfg.put_fn }>(<{ cfg.put_fn_full_params }>)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
""")


HARNESS_T = ENV.from_string(r"""
/// \file
/// <{ cfg.adapter_short }>_kernel_direct_harness.c — direct-call
/// harness for the <{ cfg.module }> property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `<{ cfg.put_fn }>`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

<{ cfg.type_text }>;

void <{ cfg.ghost_init_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>,
                              unsigned int usage);
void <{ cfg.ghost_get_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>);
void <{ cfg.ghost_put_fn }>(<{ cfg.type_param_text }><{ cfg.param_name }>);

<{ cfg.return_type }> <{ cfg.put_fn }>(<{ cfg.put_fn_full_params }>);

<{ cfg.harness_extra_decls }>
int main(void)
{
  static char <{ cfg.ghost_short }>_sentinel[1024];
  <{ cfg.type_param_text }><{ cfg.param_name }> =
    (<{ cfg.type_param_text }>)<{ cfg.ghost_short }>_sentinel;

#ifndef FIXED
  <{ cfg.ghost_init_fn }>(<{ cfg.param_name }>, 1);
#else
  <{ cfg.ghost_init_fn }>(<{ cfg.param_name }>, 2);
#endif

  (void)<{ cfg.put_fn }>(<{ cfg.param_name }><{ cfg.put_fn_extra_args }>);
  <{ cfg.ghost_put_fn }>(<{ cfg.param_name }>);

  (void)<{ cfg.put_fn }>(<{ cfg.param_name }><{ cfg.put_fn_extra_args }>);
  <{ cfg.ghost_put_fn }>(<{ cfg.param_name }>);

  return 0;
}
""")


# ---------------------------------------------------------------------
# Wiring snippets to print after generation.
# ---------------------------------------------------------------------

WIRING_SNIPPET_T = ENV.from_string(r"""
=========================================================================
Wiring for module <{ cfg.module }>
=========================================================================

1. integration/linux/scan/synthesise_harness.py — add to
   MODULE_GHOST_BOOTSTRAP (anywhere alongside the other module
   entries, but before the final closing '}' of the dict):

    "<{ cfg.module }>": {
        "types": ["<{ cfg.type_param_text }>"],
        "ghost_init_call": "<{ cfg.ghost_init_fn }>",
        "ghost_init_args_template":
            "(<{ cfg.type_param_text }>){arg}, 1",
        "ghost_init_decl":
            "void <{ cfg.ghost_init_fn }>("
            "<{ cfg.type_param_text }><{ cfg.param_name }>, "
            "unsigned int usage);",
        "forward_decls": ["<{ cfg.type_text }>;"],
        "wrapper_paths": [],
    },

2. integration/linux/scan/scan.py — add to CONTRACT_FUNCTIONS
   (anywhere in the dict):

    "<{ cfg.module }>": [
<% if cfg.is_static_inline %>        "<{ cfg.mangled_put }>",
<% endif %>        "<{ cfg.put_fn }>",
    ],

   ... and to KERNEL_ADAPTERS:

    "<{ cfg.module }>": {
        "adapter":
            SCRIPT_DIR / "adapters" / "<{ cfg.adapter_short }>_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "<{ cfg.adapter_short }>_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "<{ cfg.adapter_short }>_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "<{ cfg.module }>" / "<{ cfg.module }>.c",
        ],
        "slice_preserve": [
            "<{ cfg.live_predicate }>",
            "<{ cfg.ghost_usage_fn }>",
            "<{ cfg.ghost_init_fn }>",
            "<{ cfg.ghost_get_fn }>",
            "<{ cfg.ghost_put_fn }>",
            "<{ cfg.ghost_short }>_ghost_find",
            "<{ cfg.ghost_short }>_ghost_find_or_add",
        ],
        "required_bodies": [
            "<{ cfg.live_predicate }>",
            "<{ cfg.ghost_usage_fn }>",
        ],
    },

   ... and add "<{ cfg.module }>" to _PER_FILE_SUPPORTED_MODULES.

3. integration/linux/scan/scan-per-file.sh — add to ADAPTER_STEM
   case (the default is ADAPTER_STEM=$MODULE; we need an explicit
   mapping unless module == adapter_short):
<% if cfg.module != cfg.adapter_short %>
    <{ cfg.module }>)  ADAPTER_STEM=<{ cfg.adapter_short }> ;;
<% else %>
    (no entry needed — module name == adapter_short)
<% endif %>
   ... and to default contract targets:

    <{ cfg.module }>)
      CONTRACT_TARGETS=(
<% if cfg.is_static_inline %>        <{ cfg.mangled_put }>
<% endif %>        <{ cfg.put_fn }>
      )
      ;;

=========================================================================
""")


# ---------------------------------------------------------------------
# Built-in configs.  Used when invoked with --module <name>.
# ---------------------------------------------------------------------

BUILTIN_CONFIGS: dict[str, ModuleConfig] = {
    "device_lifetime": ModuleConfig(
        module="device_lifetime",
        type_text="struct device",
        param_name="dev",
        get_fn="get_device",
        put_fn="put_device",
        kernel_header="<linux/device.h>",
        is_static_inline=False,
        static_inline_in="",
        adapter_short="device",
        ghost_short="device",
        cve_motivation=(
            "driver-core UAFs (CVE family: 82+ CVE descriptions mention "
            "put_device, the most-cited refcount API in the 2023-2026 "
            "kernel CVE record)"
        ),
        subsystem_focus="drivers/*",
        smoke_target="drivers/base/core.c",
    ),
    "of_node_lifetime": ModuleConfig(
        module="of_node_lifetime",
        type_text="struct device_node",
        param_name="node",
        get_fn="of_node_get",
        put_fn="of_node_put",
        kernel_header="<linux/of.h>",
        is_static_inline=False,
        static_inline_in="",
        adapter_short="of_node",
        ghost_short="of_node",
        cve_motivation=(
            "Open Firmware / device tree refcount UAFs (55+ CVE "
            "descriptions mention of_node_put)"
        ),
        subsystem_focus="drivers/of, drivers/*",
        smoke_target="drivers/of/dynamic.c",
    ),
    "inode_lifetime": ModuleConfig(
        module="inode_lifetime",
        type_text="struct inode",
        param_name="inode",
        get_fn="ihold",
        put_fn="iput",
        kernel_header="<linux/fs.h>",
        is_static_inline=False,
        static_inline_in="",
        adapter_short="inode",
        ghost_short="inode",
        cve_motivation=(
            "filesystem inode UAFs (50+ CVE descriptions mention iput)"
        ),
        subsystem_focus="fs/*",
        smoke_target="fs/inode.c",
    ),
    "dentry_lifetime": ModuleConfig(
        module="dentry_lifetime",
        type_text="struct dentry",
        param_name="dentry",
        get_fn="dget",
        put_fn="dput",
        kernel_header="<linux/dcache.h>",
        is_static_inline=False,
        static_inline_in="",
        adapter_short="dentry",
        ghost_short="dentry",
        cve_motivation=(
            "filesystem dentry UAFs (47+ CVE descriptions mention dput)"
        ),
        subsystem_focus="fs/*",
        smoke_target="fs/dcache.c",
    ),
    "fput_lifetime": ModuleConfig(
        module="fput_lifetime",
        type_text="struct file",
        param_name="file",
        get_fn="get_file",
        put_fn="fput",
        kernel_header="<linux/file.h>",
        is_static_inline=False,
        static_inline_in="",
        adapter_short="fput",
        ghost_short="fput",
        cve_motivation=(
            "file struct UAFs / fdput races (34+ CVE descriptions "
            "mention fput)"
        ),
        subsystem_focus="fs/*",
        smoke_target="fs/file_table.c",
    ),
    "sock_lifetime": ModuleConfig(
        module="sock_lifetime",
        type_text="struct sock",
        param_name="sk",
        get_fn="sock_hold",
        put_fn="sock_put",
        kernel_header="<net/sock.h>",
        is_static_inline=True,
        static_inline_in="sock",
        adapter_short="sock",
        ghost_short="sock",
        cve_motivation=(
            "AF_VSOCK / netlink / Bluetooth socket UAFs (24+ CVE "
            "descriptions mention sock_put, 18+ mention sock_hold)"
        ),
        subsystem_focus="net/*",
        smoke_target="net/core/sock.c",
    ),
    "skb_lifetime": ModuleConfig(
        module="skb_lifetime",
        type_text="struct sk_buff",
        param_name="skb",
        get_fn="skb_get",
        put_fn="kfree_skb",
        kernel_header="<linux/skbuff.h>",
        is_static_inline=False,
        static_inline_in="",
        adapter_short="skb",
        ghost_short="skb",
        cve_motivation=(
            "sk_buff UAFs in network stacks and drivers/net "
            "(16+ mention skb_put, 12+ mention skb_get)"
        ),
        subsystem_focus="net/*, drivers/net/*",
        smoke_target="net/core/skbuff.c",
    ),
    "module_lifetime": ModuleConfig(
        module="module_lifetime",
        type_text="struct module",
        param_name="module",
        get_fn="try_module_get",
        put_fn="module_put",
        kernel_header="<linux/module.h>",
        is_static_inline=False,
        static_inline_in="",
        adapter_short="module",
        ghost_short="module",
        cve_motivation=(
            "module reference leaks blocking module unload "
            "(11+ CVE descriptions mention try_module_get)"
        ),
        subsystem_focus="kernel/, drivers/*",
        smoke_target="kernel/module.c",
    ),
    "kref_lifetime": ModuleConfig(
        module="kref_lifetime",
        type_text="struct kref",
        param_name="kref",
        get_fn="kref_get",
        put_fn="kref_put",
        kernel_header="<linux/kref.h>",
        # kref_put is `static inline int` in <linux/kref.h>.
        is_static_inline=True,
        static_inline_in="kref",
        adapter_short="kref",
        ghost_short="kref",
        cve_motivation=(
            "generic kref UAFs (30+ CVE descriptions mention "
            "kref_put; kref is the foundational primitive that "
            "many type-specific lifetime modules wrap)"
        ),
        subsystem_focus="kernel/, drivers/*, fs/*",
        smoke_target="lib/kobject.c",
        # kref_put takes a release callback as second arg.
        extra_params=[
            ("void (*release)(struct kref *kref)", "release"),
        ],
        return_type="int",
    ),
}


def render_all(cfg: ModuleConfig) -> dict[Path, str]:
    """Render every file the factory produces.  Returns a dict
    mapping output path -> rendered content."""
    mod_dir = PROPERTIES_DIR / cfg.module
    def rend(t: jinja2.Template) -> str:
        # Strip any single leading newline introduced by the
        # `r"""<newline>...` template literal style.
        out = t.render(cfg=cfg)
        if out.startswith("\n"):
            out = out[1:]
        return out
    out: dict[Path, str] = {
        mod_dir / f"{cfg.module}.h":     rend(HEADER_T),
        mod_dir / f"{cfg.module}.c":     rend(REF_IMPL_T),
        mod_dir / f"{cfg.module}.cocci": rend(COCCI_T),
        mod_dir / "test_unit.c":          rend(TEST_UNIT_T),
        mod_dir / "run.sh":               rend(RUN_SH_T),
        mod_dir / "README.md":            rend(README_T),
        ADAPTERS_DIR /
        f"{cfg.adapter_short}_kernel_adapter.c":
            rend(ADAPTER_T),
        ADAPTERS_DIR /
        f"{cfg.adapter_short}_kernel_adapter_probe.c":
            rend(ADAPTER_PROBE_T),
        ADAPTERS_DIR /
        f"{cfg.adapter_short}_kernel_direct_harness.c":
            rend(HARNESS_T),
    }
    return out


def write_all(rendered: dict[Path, str]) -> None:
    """Write every rendered file, creating parent directories."""
    for path, content in rendered.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        # run.sh needs to be executable
        if path.name == "run.sh":
            os.chmod(path, 0o755)


def print_wiring(cfg: ModuleConfig) -> None:
    print(WIRING_SNIPPET_T.render(cfg=cfg))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a refcount-balance property module "
                    "from a config.")
    g = parser.add_mutually_exclusive_group(required=True)
    g.add_argument("--module", help="Built-in config name "
                   "(e.g. sock_lifetime).")
    g.add_argument("--config", type=Path,
                   help="Path to a JSON config.")
    parser.add_argument(
        "--print-only", action="store_true",
        help="Print rendered files to stdout instead of writing them.",
    )
    parser.add_argument(
        "--no-wiring", action="store_true",
        help="Don't print the wiring snippet at the end.",
    )
    args = parser.parse_args()

    if args.module:
        cfg = BUILTIN_CONFIGS.get(args.module)
        if cfg is None:
            print(
                f"unknown module: {args.module}; "
                f"choices are {', '.join(sorted(BUILTIN_CONFIGS))}",
                file=sys.stderr,
            )
            return 2
    else:
        with args.config.open() as f:
            data = json.load(f)
        cfg = ModuleConfig(**data)

    rendered = render_all(cfg)
    if args.print_only:
        for path, content in rendered.items():
            print(f"=== {path} ===")
            print(content)
        return 0

    write_all(rendered)
    print(f"Wrote {len(rendered)} files for module {cfg.module}.")

    if not args.no_wiring:
        print_wiring(cfg)

    return 0


if __name__ == "__main__":
    sys.exit(main())
