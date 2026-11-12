# CBMC: `map::at` in the SMT2 datatype encoding on driver models

**Status: reduced and fixed** on branch `smt2-datatype-map-at` (commit
"SMT2: declare datatypes of pointed-to structs after the pointing struct"),
see "Resolution" at the end. The original report follows unchanged.

## Symptom

```
$ cbmc model.gb --z3 --bounds-check --unwind 12 --slice-formula
...
Passing problem to SMT2 QF_AUFBV using Z3
converting SSA
map::at
```

Exit without a verdict. Same with `--cvc5` and `--smt2`. Reproduces on four of
the five ENA proof models (all those that `#include ena_ethdev.c`); the fifth,
`harness_aenq.c`, completes normally.

## Mechanism

`map::at` is `datatype_map.at(type)` in `smt2_convt::type2id`
(`src/solvers/smt2/smt2_conv.cpp:1170`), reached for `ID_struct_tag` only when
`use_datatypes` is set:

```cpp
else if(type.id() == ID_struct_tag)
{
  if(use_datatypes)
    return datatype_map.at(type);
  else
    return "S" + std::to_string(boolbv_width(type));
}
```

`use_datatypes` is set per solver in `smt2_conv.cpp:78-125`: **true for Z3
(`:125`) and CVC5 (`:110`), unset for Bitwuzla (`:84`) and cprover-smt2
(`:96`)**. That is the whole reason Bitwuzla survives these models — it does
not exercise the datatype encoding, not because it is more robust. Every
backend configured with datatypes should be expected to hit this.

`datatype_map` is populated in `find_symbols_rec`
(`smt2_conv.cpp:6264-6275`), where the `struct_tag` case registers the type
only if its identifier is not already on the recursion stack:

```cpp
else if(type.id() == ID_struct_tag)
{
  const auto &struct_tag = to_struct_tag_type(type);
  const irep_idt &id = struct_tag.get_identifier();
  if(recstack.find(id) == recstack.end())
  {
    const auto &base_struct = ns.follow_tag(struct_tag);
    recstack.insert(id);
    find_symbols_rec(base_struct, recstack);
    datatype_map[type] = datatype_map[base_struct];
  }
}
```

The natural suspicion is a type reaching `type2id` without having been
registered by `find_symbols_rec`, so the `.at()` throws rather than returning
a default.

## What did NOT reproduce it

`smt2_datatype_shapes_NOT_REPRODUCED.c` holds five shapes, each verifying
normally under `--z3`:

1. a struct that is self-referential through a pointer;
2. a struct with an anonymous union;
3. a struct with a named union containing a struct;
4. a struct with an array of structs;
5. a struct with an array of pointers to itself.

So the trigger is none of these alone, and the correlation with
`#include ena_ethdev.c` across five models is not a diagnosis.

## Next step for whoever picks this up

Reduce from a real failing model rather than building up from synthetic
shapes. A debug build with a breakpoint on the throw would name the offending
`struct_tag` immediately, which is almost certainly faster than guessing at
type shapes — as the five failed attempts above suggest.

Suggested fix shape, once the type is known: either register the type where it
is currently skipped, or have `type2id` fall back to the `boolbv_width`
encoding instead of throwing.

## Resolution (follow-up analysis)

**The offending type.** Catching the throw in a debug session on the
freelist-equivalence model (`run_freelist_equiv.sh`, then `cbmc r0.gb --z3
--bounds-check --pointer-check --unwinding-assertions --unwind 6
--max-field-sensitivity-array-size 256 --slice-formula`) gives the call chain
`find_symbols_rec` (struct_tag) -> struct -> struct_tag -> struct, which emits a
declaration -> `convert_type` of an array component -> `datatype_map.at` of
`struct_tag` **`tag-ena_ring`**, which was skipped earlier because it was still
being processed.

**The mechanism.** `find_symbols_rec` follows pointers into the type they point
to immediately, and emits a struct's `declare-datatypes` as soon as that
struct's components are done. DPDK's `struct ena_ring` has a member
`struct ena_adapter *adapter`, and `struct ena_adapter` contains
`struct ena_ring tx_ring[...]` *by value*. So while `ena_ring` is still in
progress, the pointer leads to `ena_adapter`, which is fully processed and
declared first; its declaration needs the datatype of `ena_ring`, whose
`struct_tag` is only entered into `datatype_map` after the recursion returns.
Any model that includes `ena_ethdev.c` and keeps an `ena_ring` (or an array
of it) as a struct-typed expression has this. `aenq` completes, presumably
because no such expression survives symex there; that was not checked.

The synthetic shapes did not reproduce it because none had **a pointer to a
struct that contains the pointing struct by value**, and because a struct only
reaches the SMT2 converter when a struct-typed expression survives symex: a
single struct variable is split into its fields by field sensitivity. The
minimal reproducer needs both:

```c
struct adapter;
struct ring { struct adapter *adapter; int value; };
struct adapter { struct ring rings[2]; int n; };

int main(void)
{
  struct ring rings[2];          /* non-deterministically indexed array */
  unsigned i;
  __CPROVER_assume(i < 2);
  rings[i].adapter = 0;
  __CPROVER_assert(rings[i].value == 1, "nondeterministic");
}
```

This is `regression/cbmc/smt2-datatype-pointee-order`: `map::at` under
`--z3` and `--cvc5` on unmodified CBMC.

**Why the suggested fixes are not enough.** Registering the tag earlier (the
approach of the open upstream PR #9222 for issue #9190, which hits the same
crash through a function-pointer cycle) removes the `map::at`, but then the
datatype is named and still not declared: Z3 rejects the problem with
`unknown sort 'struct.54'` on this model. Falling back to the bit-vector
encoding in `type2id` would mix encodings for the same struct type.

**The fix.** A pointer is a bit-vector in the SMT2 encoding, so the pointed-to
type never needs declaring before the type containing the pointer.
`find_symbols_rec` now collects pointed-to types and `find_symbols` processes
them after the type being declared is complete, with the same set of visited
tags. Containment by value is acyclic, so every tag met again during the
recursion is then fully declared. The set of declared types is unchanged; only
the order differs. This also fixes the #9190 reproducer.

**Results.**

| model | before | after |
|---|---|---|
| freelist_equiv ring 4, `--z3` | map::at | VERIFICATION SUCCESSFUL, 2.8 s |
| freelist_equiv ring 4, `--cvc5` | map::at | VERIFICATION SUCCESSFUL, 3.7 s |
| freelist_equiv ring 4, `--bitwuzla` | SUCCESSFUL | SUCCESSFUL, 3.3 s |
| xmit step (`xmstep/r11.gb`), `--z3` | map::at after 16 s | no verdict in 1800 s (in the second Z3 call) |
| cleanup step (`clstep/r21.gb`), `--z3` | map::at after 6 s | no verdict in 1800 s (in the second Z3 call) |

The SAT back end decides the two step models in 40 s and 9 s, so Z3 is now
merely slow on them, which is a separate question. The `z3`, `cvc5` and
`cprover-smt2` runs of `regression/cbmc` show the same failures before and
after (the cvc5 run with a 300 s per-test limit, as `clang_builtins/bitreverse`
does not finish under cvc5 either way).

