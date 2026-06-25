# Soundness: set operations/projections on non-int (tuple) elements must not
# false-prove. The int-bitmap model cannot represent tuples; non-int membership
# is nondet and non-int adds havoc the bitmap. Each assertion below is
# genuinely FALSE, so verification must report FAILED (none may be proved).
u = {(1, 2)} | {(3, 4)}
assert (5, 6) in u          # union: spurious member
i = {(1, 2), (3, 4)} & {(1, 2)}
assert (9, 9) in i          # intersection: spurious member
d = {(1, 2), (3, 4)} - {(1, 2)}
assert (1, 2) in d          # difference: removed element
c = {(k, k) for k in range(3)}
assert len(c) == 0          # comprehension: len
a = {(1, 2)}
b = {(1, 2)}
assert a != b               # equality: equal sets are not unequal
