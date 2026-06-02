# Valid orderings must NOT be flagged: numeric-numeric (incl. int/float
# and bool/int), and str-str.
assert (3 < 5) is True
assert (5 <= 5) is True
assert (2.5 > 1) is True
assert ("a" < "b") is True
assert min(3, 1, 2) == 1
assert max(1, 2.5) == 2.5
