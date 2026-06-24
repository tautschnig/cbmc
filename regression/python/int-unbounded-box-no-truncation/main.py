# Under --python-unbounded-ints a Python int is a mathematical (arbitrary
# precision) integer. When such a value is wrapped into the tagged-union
# python_value (e.g. as a value in a heterogeneous dict/list, or any Any-typed
# slot), it must NOT be truncated mod 2**64. Before "int boxing" the
# python_value.__int_val slot was a 64-bit bitvector, so 2**64+5 silently
# became 5 -- an unsound truncation. __int_val is now a typed pointer to a heap
# mathematical integer (fixed-width pointer keeps python_value byte_extract
# valid; the integer is read through a clean dereference).


big = 2**64 + 5

# Heterogeneous dict forces the value into python_value.
d = {"a": big, "b": "x"}
assert d["a"] != 5
assert d["a"] == big

# Heterogeneous list likewise.
xs = [big, "y"]
assert xs[0] != 7
assert xs[0] == big
