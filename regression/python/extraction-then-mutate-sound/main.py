# Extraction-then-mutate (PLR reference semantics): `r = c[i]` makes `r` the SAME
# object as the container slot, so `r.append(99)` mutates `c[i]` too. The
# frontend stores nested elements by value, so the mutation would not propagate
# and `99 not in c[0]` was wrongly proved (a false proof). The guard havocs the
# source container on such a mutation, so the (false) claim no longer verifies.

c = [[1], [2]]
r = c[0]
r.append(99)
# 99 IS in c[0] at runtime; this must NOT verify.
assert 99 not in c[0]
