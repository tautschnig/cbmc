# asm-semantics build: simplify_member invariant violation during BMC

Status: fixed (linker, `src/linking/linking.cpp`). The crash surfaced in
CBMC's expression simplifier, `src/util/simplify_expr_struct.cpp:122`
(`simplify_member`), but the cause is in linking; it is independent of the
inline-assembly fixes. See "Root cause" and "Resolution" below; the earlier
sections record the investigation as it stood.

## Observation

`proof/linux/run_lx_channels.sh` (Linux ENA, `ethtool -L` then `ethtool -X`),
once the asm-goto rename defect (`asm_goto_file_local_rename.md`) is fixed so
symex runs:

    CBMC version 6.9.0 ... (both asm fixes)
    ...
    Starting Bounded Model Checking
    --- begin invariant violation report ---
    Invariant check failed
    File: src/util/simplify_expr_struct.cpp:122 function: simplify_member
    Condition: op.operands()[number].type() == expr.type()
    Reason: member expression type must match component type

## Mechanism

`simplify_member` folds `member(struct(...), .m)` by pulling out the operand of
the struct constructor at the component's index, and asserts the operand's type
equals the member-expression type. The assertion fires, so some `struct`
expression reaching the simplifier was built with a component value whose type
does not match the struct type's component -- an ill-typed struct value
constructed upstream (a byte-level reconstruction of a struct, a constant
static initializer, or a havoc of an aggregate). The two translation units in
the harness contain many kernel asm statements that are approximated
(`immediate operand 0 not scalar`, `unsupported memory operand ...`), so an
aggregate operand havocked by the conservative approximation is a candidate,
but the ill-typed value has not yet been pinned to a single source.

## Not caused by the two asm fixes

This crash sits past the point where the asm-goto rename defect used to abort
symex, so it only became reachable once that defect was fixed. It is not caused
by either asm fix:

* A build with the asm-goto operand fix but **without** the sub-register
  zero-extend change (commit `82425d3baa7` only) crashes with the identical
  invariant at the same point, so the widening change is not responsible.
* Neither fix constructs `member` or `struct` expressions; the operand fix
  moves the asm-goto target into an operand, and the widening fix only changes
  whether a register temporary is a constant or has non-deterministic upper
  bits. A `member`/component type mismatch is structural, not value dependent.

So the defect predates this work and lives in the lowering of some aggregate
value (framework or CPROVER library), merely unmasked by the rename fix.

## Reproducer

Not yet reduced. Reproduced end to end via `run_lx_channels.sh` with the ENA
driver goto binaries rebuilt by the fixed goto-cc
(`build_linux_ena.sh`). Next step: narrow it with `--show-vcc` /
`goto-instrument --dump-c` around the approximated aggregate operands, or bisect
the harnessed translation units, to obtain a minimal struct value that trips
the invariant.

## Workaround

None yet. The harness reaches BMC (the asm-goto blocker is gone) but cannot
complete until this simplifier invariant is addressed.

## Root cause

The `member` expression reads `$anon0` of a constant `struct ena_ring` value
built by the harness (`harness_lx_channels.c` line 79). The member's type and
the stored operand's type are two *different* union tags, both naming the same
anonymous union of `ena_netdev.h` line 337.

The C front end names an anonymous struct/union after its structure,
including the definitions of all types reachable through its pointers
(`tag-#anon#UN[...]`). Deep in that chain is `struct uts_namespace`, complete
in one translation unit and only declared in the other, so the two units give
the anonymous union different identifiers (they first differ some 38,600
characters in). `linkingt::needs_renaming_type` accepts the two `struct
ena_ring` definitions as structurally equivalent (commit `0414b43bf21`) and
keeps the main-table one, whose `$anon0` has the first unit's tag. But the
second unit's code and values kept referring to *its* tag, so the linked
program mixed two tags for one C type.

Minimal reproducer: `regression/cbmc/Linking13` (a nested struct complete in
`main.c`, only declared in `module.c`, a struct value built in `module.c`).
Without the fix it fails `member_exprt` validation; in the harness, the same
mismatch reaches `simplify_member` first.

## Resolution

When structural equivalence is accepted, the linker now records every
anonymous-tag pair the check matched up in `rename_new_symbol`, so the new
unit's references are rewritten to the kept tags like any other renamed type
(commit "Linking: map equivalent anonymous tags of the new unit to the kept
ones"). `Linking13` passes with `--validate-goto-model
--validate-ssa-equation`, and the harness no longer aborts.
