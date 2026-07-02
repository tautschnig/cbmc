# PLR §3.3.1: a comprehension over a provably non-iterable scalar raises
# TypeError (`[x for x in None]`). Same shared predicate as for-loop / unpack.
# CPython: TypeError; expected: VERIFICATION FAILED.
r = [x for x in None]
