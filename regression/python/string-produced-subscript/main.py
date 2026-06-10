# String backend phase 2: byte-level / chained operations on a
# refinement-produced string result (here a concatenation with symbolic
# content) read real backing memory, so subscript and iteration of the
# produced result work -- and are loop-safe.
i: int = nondet_int()
__ESBMC_assume(i == 102)  # 'f'

s: str = chr(i) + "oo"  # "foo": a produced (concat) result with symbolic byte

# Subscript of the produced result (was unsupported: content had no backing).
assert s[0] == "f"
assert s[1] == "o"
assert s[2] == "o"

# Iteration of the produced result inside a loop (loop-safe: fresh backing
# per dynamic substring call).
seen_f: bool = False
for c in s:
    if c == "f":
        seen_f = True
assert seen_f
