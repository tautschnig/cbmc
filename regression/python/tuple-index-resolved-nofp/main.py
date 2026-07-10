# CORE no-false-alarm: a resolved tuple's .index (present element) stays precise
# -- the may-raise only applies to the unresolved (Any) receiver.
t = (5, 8, 3)
assert t.index(8) == 1
u = tuple([1, 2, 3])
assert u.index(2) == 1
