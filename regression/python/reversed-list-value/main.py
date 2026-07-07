# PLR §6.2.9: reversed(list) yields elements in reverse. It had returned the list
# UNCHANGED (reversed(xs) == xs, a false proof found by the negated value-oracle).
# Now precise.
xs = [1, 2, 3]
assert list(reversed(xs)) == [3, 2, 1]
assert list(xs) == [1, 2, 3]
assert len(list(reversed([5, 6]))) == 2
