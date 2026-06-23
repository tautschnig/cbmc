# set.pop() returns an actual element (its bit is set), so it is precise for
# singletons and sound (element-constrained) for multi-element sets.
s = {1, 2, 3} - {1} - {3}
assert s.pop() == 2

assert {5}.pop() == 5

t = {1, 2}
x = t.pop()
assert x == 1 or x == 2
assert len(t) == 1
