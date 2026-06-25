# List construction / reshape precision (P1): list(<tuple>) and slice-assignment
# `a[i:j] = <list>` were broken even for homogeneous element types (the result
# was a nondet list, so even len() was unknown). Both are now precise.

# list(<tuple>): materialise a list from the tuple's fields.
a = list((1, 2, 3))
assert len(a) == 3
assert a[0] == 1 and a[2] == 3
b = list(reversed((1, 2, 3)))
assert b[0] == 3

# Slice-assignment, same length: a[0:2] = [7,8] -> [7,8,3]
c = [1, 2, 3]
c[0:2] = [7, 8]
assert len(c) == 3
assert c[0] == 7 and c[1] == 8 and c[2] == 3

# Slice-assignment, growing: a[1:2] = [7,8,9] -> [1,7,8,9,3]
d = [1, 2, 3]
d[1:2] = [7, 8, 9]
assert len(d) == 5
assert d[1] == 7 and d[4] == 3

# Slice-assignment, shrinking: a[1:3] = [9] -> [1,9,4]
e = [1, 2, 3, 4]
e[1:3] = [9]
assert len(e) == 3
assert e[1] == 9 and e[2] == 4
