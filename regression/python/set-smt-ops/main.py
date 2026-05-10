# Python set methods over the bitmap representation.
# Precise for int elements in [0, 64).

s = {1, 2, 3}
t = {2, 3, 4}

# add
s.add(5)
assert 5 in s

# discard (no-op if absent)
s.discard(99)
assert 99 not in s

# union / intersection / difference / symmetric_difference
u = s.union(t)
assert 1 in u and 4 in u

i = s.intersection(t)
assert 2 in i and 3 in i
assert 1 not in i

d = s.difference(t)
assert 1 in d and 5 in d
assert 2 not in d

x = s.symmetric_difference(t)
assert 1 in x and 4 in x and 5 in x
assert 2 not in x

# issubset / issuperset / isdisjoint
assert {2, 3}.issubset(s)
assert s.issuperset({2, 3})
assert s.isdisjoint({99, 100})
assert not s.isdisjoint(t)

# copy
c = s.copy()
assert 1 in c

# clear
s.clear()
# after clear, nothing is in the set
assert 1 not in s
