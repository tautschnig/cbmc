# CBMC: `map::at` in the SMT2 datatype encoding on driver models

**Status: localised, not yet reduced.** Reported here so the next attempt
starts from the mechanism rather than the symptom.

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
