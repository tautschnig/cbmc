# PLR §6.10.2: a tuple boxed into a python_value (e.g. an element of a
# heterogeneous list) is tagged TUPLE (not the generic CLASS fallback), so
# isinstance(x, tuple) recognises it, and it is NOT a str/int/list.
xs = [(1, 2), 9]
assert isinstance(xs[0], tuple)
assert isinstance(xs[0], (int, tuple))
assert not isinstance(xs[0], int)
assert not isinstance(xs[0], list)
assert not isinstance(xs[1], tuple)
