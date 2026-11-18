# The assertion fails in CPython.
# A list passed to a parameter of type Any was copied into a value and its
# changes were lost; and `f(3); f(xs)` inferred int for the parameter and
# passed xs as 0.
def f(a):
    if isinstance(a, list):
        a.append(1)


f(3)
xs = []
f(xs)
assert len(xs) == 0, "any_param"
