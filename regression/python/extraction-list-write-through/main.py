# §0 write-through, LIST-element form: `r = g[0]` returns the data[0]
# lvalue with a CONSTANT index -- re-emittable verbatim, so an in-place
# mutation of r writes back through the slot precisely (the dict slot
# and string-key forms landed earlier; this completes the container
# matrix). A SYMBOLIC index is never recorded (its variable could be
# reassigned by a statement that never mentions the source -- a false
# write); reorderings (insert/pop/sort) mention the source and demote to
# the sound havoc floor (pinned by extraction-mutation-channels and the
# earlier hazard suites).
g = [[1], [2]]
r = g[0]
r.append(5)
assert len(g[0]) == 2
assert len(g[1]) == 1
assert g[0][1] == 5

h = [[9]]
s = h[0]
s[0] = 7
assert h[0][0] == 7

d = {1: [9]}
v = d[1]
v += [5]
assert len(d[1]) == 2
# (element-value reads after aug-assign are a pre-existing precision
# limit independent of the write-through: `v=[9]; v+=[5]; v[1]==5` is
# not proven standalone either -- lists-cluster item.)

# pv-typed slots: the write-back's wrap_value SNAPSHOT must be taken
# POST-statement (its materialisation is spliced into the post queue);
# a pre-statement snapshot stored the stale value -- a false proof
# caught by annotation-extract-mutate-guarded during this work.
from typing import Any

pd: dict[int, Any] = {1: [9]}
pv = pd[1]
pv.append(5)
assert len(pd[1]) == 2
