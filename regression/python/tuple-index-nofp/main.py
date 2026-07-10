# No-false-positive: present element, and count() of present/absent.
t = (3, 7, 3)
assert t.index(7) == 1
assert t.count(3) == 2
assert t.count(9) == 0
