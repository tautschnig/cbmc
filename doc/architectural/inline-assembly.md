\ingroup module_hidden

\page inline-assembly Inline Assembly

\author Kiro

This page describes how CBMC gives meaning to inline assembly, which
guarantees it makes, and how to add support for further instructions and
architectures.

## Guarantee

Inline assembly is lowered to ordinary goto-program instructions by
`remove_asm` (`src/assembler/`), which every tool runs before linking the C
library. The lowering is **sound by construction**: every `asm` statement is
either

1. **interpreted**: every instruction in its template is understood, and the
   statement is replaced by goto code with the same effect on the C-visible
   state as the instructions, or
2. **approximated**: at least one instruction is not understood, and the
   statement is replaced by code that havocs everything the statement is
   permitted to change (see "Conservative approximation" below), and a
   warning naming the statement and the instruction is printed.

There is no third outcome. Before this design, a statement containing an
unknown instruction was silently deleted, so CBMC assumed that it had no
effect.

What "the C-visible state" means is made precise below. In short, it consists
of the C objects (variables, heap objects) and the values of the C
expressions bound to output operands. Processor state that C cannot observe
(registers that are not bound to operands, flags, privileged state, I/O ports,
other address spaces) is modelled only as far as the instructions of the same
statement can observe it.

## The GCC operand interface

A GCC-style statement is

```
asm [volatile] [goto] ( template : outputs : inputs : clobbers : labels );
```

The parser keeps all of it in a `code_asm_gcct` (see `util/std_code.h`):
the template, the outputs and inputs as `gcc_asm_output`/`gcc_asm_input`
pairs `(constraint, expression)`, optionally with the symbolic name used in
`%[name]` references in `ID_identifier`, the clobbers as
`gcc_asm_clobbered_register` strings, and the labels of `asm goto`.

`asm_operandst` (`asm_operands.h`) parses the constraints and classifies each
operand:

| Constraint | Class | Meaning in the model |
|---|---|---|
| `=`, `+` prefix | output | `+` is also read |
| `&` | early clobber | irrelevant here: every operand gets its own storage |
| `m`, `o`, `V`, `Q` (AArch64), `Q`/`Um`/`Uv` (ARM) | memory | the operand **is** the C lvalue; accesses go to memory as they happen |
| `r`, `q`, `l`, `w` and the like | register | a block-local temporary of the expression's type |
| `a`, `b`, `c`, `d`, `S`, `D` (x86) | fixed register | as `r`, and also the value of that hardware register |
| `i`, `n`, `I`..`P`, `e`, `Z`, `K`, `L`, `M`, `N` | immediate | the value |
| `g`, `rm`, `ri`, `Nd`, `er` ... | alternatives | the first applicable class above |
| digit, `[name]` | matching | initialises the storage of the matched output |
| `=@cc<cond>` (x86), `=@cc<cond>` (AArch64) | flag output | after the block, the output receives `<cond>` evaluated on the flags |

The sequencing follows GCC's semantics: all inputs are evaluated before the
first instruction; register outputs are copied back to their lvalues after the
last instruction; memory operands are accessed in place. Values read from a
register wider than the bound C expression have unspecified upper bits, which
the model makes non-deterministic.

When an input operand narrower than a register is loaded into a register
temporary, the value is zero-extended to the register width, so a constant
input stays constant and symex can propagate it. The only exception is an
operand some reference reads above its natural width (an operand modifier that
names a wider sub-register, see `asm_semanticst::reference_widens`): there the
upper bits are unspecified, so they are made non-deterministic. Making the
common case constant matters for performance as well as precision -- a lock
word written through a sub-register input must stay concrete, or the spin
loops around it become undecidable and unwind to the bound.

## Lowering pipeline

For each `asm` statement, `remove_asmt`:

1. builds an `asm_operandst` and an `asm_lowering_contextt` (the goto program
   being produced, temporaries, labels, flags, registers);
2. asks the `asm_semanticst` object for the configured architecture
   (`config.ansi_c.arch`, see `make_asm_semantics`) to split the template into
   instructions (`asm_templatet`: separators, comments, local labels,
   directives, and `%`-operand references with their modifiers);
3. lowers each instruction with `asm_semanticst::lower`; this returns `false`
   for anything it does not understand;
4. if every instruction was lowered, emits the result, wrapped in an atomic
   section if any instruction was atomic; otherwise discards it and emits the
   conservative approximation, with a warning.

The architecture is the one CBMC is configured for (`--arch`, or the one
recorded in a goto binary), because the same mnemonic means different things
on different architectures and because register names, comment characters and
operand modifiers differ. A template that does not parse for the configured
architecture is approximated, never guessed.

## Conservative approximation

When a statement is not understood, it is replaced by:

1. evaluation of the inputs (so that their side effects and undefined
   behaviour checks are kept);
2. a non-deterministic assignment to every output lvalue (`=` and `+`);
3. if the clobbers contain `"memory"`:
   * `__CPROVER_havoc_object` of every object pointed to by a pointer-typed
     input or output operand, and
   * a non-deterministic assignment to every non-constant variable with static
     lifetime (globals and static locals), other than CPROVER internals;
4. for `asm goto`, a non-deterministic branch to each label.

GCC's documentation lets an asm with a `"memory"` clobber read and write any
memory. Item 3 covers the memory reachable from the statement's operands and
everything with static lifetime; heap or stack objects reachable only through
pointers held elsewhere are not havocked. That residual gap is stated in the
warning.

An **interpreted** statement does not do any of this: a `"memory"` clobber is
a statement about what the instructions *may* touch, so it does not add
effects to instructions whose effect is known. In particular
`asm volatile("" ::: "memory")`, the compiler barrier used by
`barrier()`, has no effect on the C state, which is correct: it executes no
instruction.

MSVC-style `__asm` blocks have no operand interface. When not understood, they
are approximated by havocking every C variable named in the block, the objects
those variables point to, and all variables with static lifetime.

## Atomicity and memory ordering

Atomic instructions (x86 `lock`-prefixed instructions and `xchg` with a memory
operand; AArch64 load/store-exclusive pairs and LSE atomics; AArch32
`ldrex`/`strex`) make the whole statement an atomic section
(`ATOMIC_BEGIN`/`ATOMIC_END`), which is what symex and the weak-memory
instrumentation need. Barrier instructions are lowered to `fence` statements
as before.

A store-exclusive (`stxr`, `strex`) inside such an atomic section always
succeeds. The architecture allows spurious failure, but a well-formed
load/store-exclusive sequence retries until it succeeds, and inside an atomic
section no other thread can intervene, so the successful iteration is the
only one that matters. Code that does not retry would be treated as if its
store had succeeded; this is the one place where the model deliberately does
not include every behaviour the architecture permits, and it is documented
here for that reason.

## Environment instructions

Some instructions read or write state that the C program cannot observe
through ordinary memory accesses: I/O ports (`in`, `out`), model-specific and
system registers (`rdmsr`, `wrmsr`, `mrs`, `msr`), timers and identification
(`rdtsc`, `rdtscp`, `cpuid`), segment-relative memory (x86 `%gs:` and `%fs:`
operands, used for per-CPU data), interrupt control (`cli`, `sti`,
`msr daifset`), and hints (`pause`, `yield`, `wfe`, `prfm`). These are
interpreted as follows: reads produce non-deterministic values, writes have no
C-visible effect. Segment-relative memory is therefore modelled as a separate,
unconstrained address space; C code that reaches the same memory through an
ordinary pointer (for example by computing a per-CPU pointer) is not
connected to it.

## Adding instructions

Instructions are lowered by the `asm_semanticst` subclass for the
architecture. Each handles one mnemonic (with its size and condition
variants) in one function, using the context's helpers:

* `ctx.read(operand, width)` and `ctx.write(operand, width, value)`
  for register, memory and immediate operands;
* `ctx.address(operand)` for memory operands;
* `ctx.flags()` for the condition flags;
* `ctx.branch(label, condition)` for jumps to local labels (`1b`, `2f`),
  `asm goto` labels (`%l[name]`) and named local labels;
* `ctx.mark_atomic()` and `ctx.fence(...)`.

Rules for a new instruction:

1. Model its effect on the C-visible state exactly. If any part of it cannot
   be modelled exactly, make that part non-deterministic, and say so in a
   comment. Never drop an effect.
2. Reject (return `false`) any operand form you do not handle. Rejection is
   always safe; it leads to the conservative approximation.
3. Add a regression test under `regression/cbmc/asm-<arch>/` that checks the
   effect positively (a property that holds) and negatively (the property
   that would hold if the instruction were ignored must fail).

## Adding an architecture

1. Subclass `asm_semanticst` in `asm_<arch>.{h,cpp}`:
   * `comment_starts()` and `separators()` for the template splitter;
   * `parse_register(name)` returning the register's identity and width, and
     how writes to sub-registers affect the full register (x86 merges 8- and
     16-bit writes and zero-extends 32-bit writes; AArch64 zero-extends
     `w`-register writes);
   * `apply_modifier(modifier, operand)` for the `%`-operand modifiers of the
     architecture's GCC back end (x86 `b`, `h`, `w`, `k`, `q`, `c`, `P`;
     AArch64 `w`, `x`, `c`; ARM `c`, `Q`, `R`, `H`);
   * the flags of the architecture, and the condition-code names used by
     branches and `=@cc` outputs;
   * `lower(instruction, ctx)`.
2. Register it in `make_asm_semantics` for the `config.ansi_c.arch` values
   it serves.
3. Add the fixed-register constraint letters of the architecture to
   `asm_operandst` if it has any.
4. Add regression tests as above, including one for an instruction that is not
   supported, checking that the conservative approximation applies.

Until an architecture is registered, every statement compiled for it is
approximated, which is sound.

## Supported instructions

Kept in sync with the implementation; the regression tests under
`regression/cbmc/asm-*` exercise each group.

* **x86 (i386, x86_64), AT&T syntax**: `mov`, `movzx`/`movs`/`movz` forms,
  `lea`, `add`, `adc`, `sub`, `sbb`, `and`, `or`, `xor`, `not`, `neg`, `inc`,
  `dec`, `shl`/`sal`, `shr`, `sar`, `rol`, `ror`, `bt`, `bts`, `btr`, `btc`
  (including bit offsets beyond the operand for memory operands), `bsf`,
  `bsr`, `tzcnt`, `lzcnt`, `popcnt`, `bswap`, `xchg`, `xadd`, `cmpxchg`,
  `cmp`, `test`, `set<cc>`, `cmov<cc>`, `j<cc>`, `jmp`, with operand-size
  suffixes and the `lock` and `rep` prefixes; `nop`, `pause`, `mfence`,
  `lfence`, `sfence`, `fstcw`, `fnstcw`, `fldcw`; environment instructions
  `in`, `out`, `rdtsc`, `rdtscp`, `cpuid`, `rdmsr`, `wrmsr`, `cli`, `sti`,
  `hlt`, segment-relative operands.
* **AArch64**: `mov`, `mvn`, `add`, `sub`, `and`, `orr`, `eor`, `bic`,
  `lsl`, `lsr`, `asr`, `neg`, `cmp`, `cmn`, `tst`, `cset`, `csel`, `b`,
  `b.<cc>`, `cbz`, `cbnz`, `tbz`, `tbnz`, `ldr`/`str` (`b`, `h`, `w`, `x`
  forms, immediate offsets), `ldar`/`stlr`, `ldxr`/`stxr`/`ldaxr`/`stlxr`
  (with sizes), LSE `ldadd`, `ldclr`, `ldeor`, `ldset`, `swp`, `cas` (with
  `a`, `l`, `al` and size variants) and their `st` aliases, `dmb`, `dsb`,
  `isb`, `nop`, `yield`, `wfe`, `wfi`, `sev`, `sevl`, `prfm`; environment
  `mrs`, `msr`; directives `.arch_extension`, `.arch`.
* **AArch32**: `mov`, `mvn`, `add`, `sub`, `rsb`, `and`, `orr`, `eor`,
  `bic`, `cmp`, `cmn`, `tst`, `teq`, conditional execution suffixes,
  `b<cc>`, `ldr`/`str` (byte, halfword), `ldrex`/`strex` (with sizes), `dmb`,
  `dsb`, `isb`, `nop`, `yield`, `wfe`, `sev`; environment `mrc`, `mcr`,
  `cpsid`, `cpsie`.
* **Power**: the barriers `sync`, `lwsync`, `isync` (as before).

## Contracts for inline assembly (design, not yet implemented)

The sound default makes a property that depends on an unsupported statement
unprovable rather than falsely proven, and the interpreted instructions cover
the kernel and driver idioms that motivated this work. What it does not do is
let a user *specify* the effect of a statement CBMC cannot interpret, so that a
proof can still go through. That is what a contract on an asm statement would
add, and it fits the existing machinery:

* **Syntax.** Accept the clause keywords CBMC already has, `__CPROVER_assigns`,
  `__CPROVER_ensures` and `__CPROVER_requires`, after the operand lists of a
  gcc-style statement (a new alternative of `gcc_asm_statement` in
  `parser.y`), and store them on the `code_asmt` as `ID_C_spec_assigns` /
  `ID_C_spec_ensures` / `ID_C_spec_requires` sub-ireps, next to the operands.
  They survive goto conversion because `convert_asm` copies the whole code.
* **Type checking.** The clause expressions reference the C lvalues in scope,
  including the asm's own output operands, so they type-check exactly like a
  function contract's clauses (`c_typecheck_code`).
* **Lowering.** In `remove_asm`, a statement that carries a contract is
  lowered from the contract instead of from the template, whether or not the
  template is understood: `requires` becomes an assumption, the `assigns`
  targets are havocked (reusing `havoc_assigns_targetst` from
  `goto-instrument/contracts/utils.h`, which is exactly the precise-havoc the
  conservative approximation lacks), and `ensures` becomes an assumption on
  the post-state. A `requires`/`ensures` pair could additionally be *checked*
  against the interpreted semantics when the template is understood, as a
  consistency test, under an option.
* **Soundness.** As with function contracts, a supplied contract is trusted at
  its use and so is only as sound as the user makes it; that is the point of
  the escape hatch. The default for a statement with no contract is unchanged:
  interpret, or approximate conservatively.

This is a self-contained change of a size comparable to loop contracts and is
left as the recommended next step.


## Not covered

* Top-level (file-scope) `asm` is ignored by the parser; it cannot affect C
  state except by defining symbols, which CBMC does not model.
* Intel-syntax templates under GCC (`.intel_syntax`) are approximated.
* Raw machine code (`.byte`, `.inst`) is approximated.
* Code placed into other sections (`.pushsection` ... `.popsection`, as used
  by the Linux kernel's alternatives and exception tables) is not part of the
  statement's execution and is skipped; the fall-through code is lowered.
