# §0 slot-alias WRITE-THROUGH (spike doc §14b): `v = d[k]` records the
# materialised lvalue slot; an in-place mutation of v writes back through
# it (pending_post_checks), so the extraction-then-mutate shape is PRECISE
# without runtime pointers (the §12 equality cost wall does not apply).
# Straight-line, non-escaped, int-keyed dict only; everything else demotes
# to the sound havoc (see extraction-slot-demotion-hazards).
d = {1: [1]}
v = d[1]
v.append(2)
assert len(d[1]) == 2
assert d[1][0] == 1
assert d[1][1] == 2
