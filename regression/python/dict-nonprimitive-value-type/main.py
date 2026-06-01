# Differential unsoundness witness §9 (cbmc-py-differential UNSOUNDNESS.md):
# a dict annotated with a non-primitive value type, e.g.
# `dict[str, list[int]]`, must model the value as that container,
# not degrade it to int. Previously the value type fell back to int,
# so `d["a"]` was an int and `len(d["a"])` diverged from CPython.
# Now fixed — the value is a list and len() is exact.
d: dict[str, list[int]] = {"a": [1, 2, 3]}
v = d["a"]
assert len(v) == 3
assert v[0] == 1
assert v[2] == 3
