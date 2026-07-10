# PLR §6.10.1: a list[python_value] whose elements are pointer-backed references
# (nested list, dict, set, tuple) must compare by VALUE, not by heap pointer. A
# plain equal_exprt compares the union bits (incl. the reference pointer), so two
# equal-but-distinct nested values compared unequal -> `!=` was wrongly PROVED.
# Now element comparison is tag-aware (python_value_structural_eq). CPython:
# xs == ys is True, so `xs != ys` is False -> AssertionError (verification FAILS).
a = [1, 2]
xs = [a, 3]
ys = [[1, 2], 3]
assert xs != ys
