# Reference-semantics robustness (--python-ref-mutables): the value-semantic
# boundaries for reference lists are correct and do not crash.
# See doc/python-frontend-reference-semantics-spike.md.

# (1) A nested list literal passed to a parameter annotated with a concrete
# nested list type used to abort in value_set::assign (list[python_value] vs
# list[list[int]]); the annotation is now lowered to the reference
# representation uniformly.
def take(items: list[list[int]]) -> int:
    return len(items)
assert take([[1, 2], [3, 4]]) == 2

# (2) Concatenation with a reference element: `[1] + r` where r is an extracted
# inner list reference. The reference is dereferenced and the element types are
# unified before merging.
g = [[10, 20]]
r = g[0]
joined = [1] + r
assert len(joined) == 3
assert joined == [1, 10, 20]

# (3) extend with a concatenation involving a reference element: extend adds
# the ELEMENTS of `[0] + row`, so acc grows by len([0]+row).
def rows() -> list[list]:
    return [[7]]
acc = []
for row in rows():
    acc.extend([0] + row)
assert len(acc) == 2
assert acc[1] == 7
