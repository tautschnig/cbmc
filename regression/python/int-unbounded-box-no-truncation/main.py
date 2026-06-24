# Under --python-unbounded-ints a Python int is the mathematical integer_typet.
# Typed positions store it inline at full precision; an int wrapped into the
# python_value tagged union (Any-typed / heterogeneous-container value) is boxed
# behind a fresh per-instance heap integer, so it ALSO keeps full precision (no
# truncation mod 2**64) and does not alias across instances.

# Typed full precision.
x = 2**100
assert x > 2**99
assert x + 1 > x

# Wrapped (heterogeneous dict value -> python_value) keeps full precision.
big = 2**64 + 5
d = {"a": big, "b": "x"}
assert d["a"] == big
assert d["a"] != 5

# Wrapped (heterogeneous list) keeps full precision.
xs = [big, "y"]
assert xs[0] == big
