# PLR §6.3.3: a step slice (|step| >= 2) must NOT silently return the step-1
# (contiguous) slice. `xs[::2]` is [1,3,5], so cbmc must not prove `xs[::2] !=
# [1,3,5]` (it did, returning the whole list). Now over-approximated soundly
# (nondet, length-bounded), so the negated assert cannot be proved. Found by the
# negated value-oracle fuzzer. CPython: AssertionError; expected: FAILED.
xs = [1, 2, 3, 4, 5]
assert xs[::2] != [1, 3, 5]
