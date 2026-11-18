# The changes a callee makes to a list passed to a parameter of type Any.
def f(a):
    if isinstance(a, list):
        a.append(1)


f(3)
xs = [5]
f(xs)
assert len(xs) == 2 and xs[0] == 5 and xs[1] == 1
