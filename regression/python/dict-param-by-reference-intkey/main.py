# With the uniform dict[value, value] default (enabled by inlining the
# refined string into python_value), a NON-string-keyed dict argument
# also passes by reference: keys and values both widen to the tagged
# union through the generic container boundary, and mutations propagate.
# This closes the last dict pass-by-reference residual.

def put(d: dict, k: int, v: int):
    d[k] = v

m = {1: 10}
put(m, 2, 20)
assert m[2] == 20
assert m[1] == 10
assert len(m) == 2
