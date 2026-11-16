# asm-semantics build: asm goto + `--export-file-local-symbols` aborts symex

Status: crash (invariant violation) in the asm-semantics branch
(`e09f7f3a755`). Blocks every Linux kernel harness that calls a `static`
driver function, because those need `goto-cc --export-file-local-symbols`
and the kernel headers put `asm goto` into `arch_static_branch` (static keys),
which nearly every kernel translation unit includes.

## Observation

`proof/linux/run_lx_channels.sh` (Linux ENA, `ethtool -L` then `ethtool -X`):

    Invariant check failed
    File: src/goto-symex/renaming_level.cpp:36 function: symex_level0
    Condition: found_l0
    Reason: level0: failed to find arch_static_branch::$tmp::tmp_asm_goto_target

## Reproducer

`asm_goto_file_local_rename.c` / `.sh`: one translation unit, one static
inline function containing `asm goto`, compiled with and without the flag.

    build (no flag): VERIFICATION SUCCESSFUL
    build --export-file-local-symbols: VERIFICATION SUCCESSFUL
    build-asm (no flag): VERIFICATION SUCCESSFUL
    build-asm --export-file-local-symbols: Reason: level0: failed to find arch_static_branch::$tmp::tmp_asm_goto_target

The goto binary built with the flag contains both
`__CPROVER_file_local_..._arch_static_branch::$tmp::tmp_asm_goto_target`
(the renamed symbol, used by the `DECL` and the dispatching `IF`s) and the
old name `arch_static_branch::$tmp::tmp_asm_goto_target`.

## Mechanism

`goto_asm.cpp` (goto conversion of `asm goto`) creates the temporary that
records which label the asm chose and stores it on the asm statement as a
named sub-tree, `new_code.add(ID_asm_goto_target, goto_target)`.
`--export-file-local-symbols` renames the function's symbols with
`function_name_manglert` (`src/goto-programs/name_mangler.h`), which applies
`rename_symbolt` to each instruction's code. `rename_symbolt::rename(exprt&)`
(`src/util/rename_symbol.cpp`) walks `depth_begin()`, i.e. operands and
types, plus two specific comment subs; it never visits `asm_goto_target`. The
asm statement therefore keeps the old identifier, and `remove_asm.cpp:311`
lowers the statement into an assignment to a symbol that no longer exists.

Any other pass that renames through `rename_symbolt` or `replace_symbolt`
(linking collisions, for instance) would leave the same stale reference.

## Possible fixes (for the asm-semantics author)

* Carry the goto-target symbol as an operand of the asm statement, so every
  renaming and replacement pass sees it.
* Or create the temporary in `remove_asm` rather than in goto conversion,
  so no pass between the two can rename it. The dispatch `IF`s generated in
  `goto_asm.cpp` would then have to be generated there as well.

## Workaround

None found that keeps the inline-assembly semantics. The old build treats
the asm as having no effect, which is the soundness problem the
asm-semantics branch fixes (`inline_asm_assumed_no_effect.md`). Reaching
the static entry points through `netdev->ethtool_ops` instead of calling
them directly would drop the need for the flag, but other static helpers
still get renamed when same-named statics in several translation units are
linked, and that path has not been checked.

## Resolution

Fixed by the first suggested fix. The goto-target temporary is carried as the
optional sixth operand of `code_asm_gcct` (`has_goto_target()`,
`goto_target()`, `add_goto_target()` in `src/util/std_code.h`) instead of the
named sub `ID_asm_goto_target`, which has been removed. `rename_symbolt` and
`replace_symbolt` walk operands, so `--export-file-local-symbols` (and any
other pass renaming through them) now rewrites the target with the rest of the
function body. `goto_asm.cpp`, `remove_asm.cpp`, `asm_approximation.cpp` and
`expr2c.cpp` read the target through the new accessors.

With the fix, `build-asm --export-file-local-symbols` on the reproducer is
VERIFICATION SUCCESSFUL and the stale `arch_static_branch::$tmp::
tmp_asm_goto_target` identifier is gone. Regression test
`regression/goto-cc-file-local/asm-goto` pins it (asm goto in a static
function, compiled and linked with the flag, symex succeeds). This unblocks
the Linux kernel harnesses that call `static` driver functions.
