# & - ^ and their augmented forms on sets of strings.
u = {"a", "b"}
assert u & {"b", "z"} == {"b"}
assert u ^ {"b", "c"} == {"a", "c"}
v = {"a", "b"}
v -= {"a"}
assert v == {"b"}
