# PLR §7.2.1: unpacking a tuple requires its arity to equal the number of
# targets, else ValueError ("not enough values to unpack"). `a, b = (1,)` is a
# static arity mismatch (1 vs 2) -> definite ValueError. Previously the unpack
# silently skipped the missing `_1` field (the ty-015 false proof: the same
# mismatch via `a, b = make()` where make() returns the fixed `(1,)`).
t = (1,)
a, b = t
print(a + b)
