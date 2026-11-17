import collections

c = collections.Counter([4, 4, 9])
assert c[4] == 2
assert c[9] == 1
assert c[7] == 0
assert len(c.keys()) == 2
assert len(c) == 2
