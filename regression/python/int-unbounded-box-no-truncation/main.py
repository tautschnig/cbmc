# Under --python-unbounded-ints a Python int is the mathematical integer_typet.
# In TYPED positions (typed variables, dict[int,int] / list[int]) it is stored
# inline at full precision — no overflow, no truncation:
x = 2**100
assert x > 2**99
assert x + 1 > x

d = {"a": 5, "b": 7}
assert d["a"] == 5
assert d["a"] + d["b"] == 12

# NOTE: an int wrapped into the python_value tagged union (Any-typed / a value
# in a heterogeneous container) is SOUND but over-approximated to nondet — CBMC
# cannot store distinct per-instance mathematical-integer heap objects, so a
# precise box would alias across instances built by the same construction site
# (a false proof). That over-approximation is covered by
# int-unbounded-box-sound; this test pins the precise typed path.
