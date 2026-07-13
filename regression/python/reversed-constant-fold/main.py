# CORE (PLR §6.2.9) [precision]: reversed() over a constant list now folds its
# constant elements at conversion time -> a CONSTANT struct that downstream
# constant-folds recognise (mirrors sorted()). Previously reversed() built a
# SYMBOLIC struct (index reads of the source data), so tuple(reversed(const))
# could not fold (tuple()'s fold needs a constant length) -- a false alarm.
assert tuple(reversed([1, 2, 3])) == (3, 2, 1)
xs = [4, 5, 6]
assert tuple(reversed(xs)) == (6, 5, 4)
assert list(reversed(["a", "b"])) == ["b", "a"]
# correctness: reversed is a genuine reversal, not the identity
assert reversed([1, 2, 3]) != [1, 2, 3]
