# asm-semantics build: simplify_member invariant violation during BMC

Status: crash (invariant violation) in CBMC's expression simplifier,
`src/util/simplify_expr_struct.cpp:122` (`simplify_member`). Independent of the
inline-assembly representation and widening fixes; see below.

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
