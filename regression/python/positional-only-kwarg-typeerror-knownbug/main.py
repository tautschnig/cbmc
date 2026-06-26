# KNOWNBUG (false proof): a positional-only parameter (before `/`) passed by
# keyword raises TypeError in CPython ("got some positional-only arguments passed
# as keyword arguments: 'x'"). cbmc binds it and succeeds.
def func(x, /, y):
    return x + y

r = func(x=1, y=2)   # CPython TypeError (x is positional-only)
assert True          # unreachable in CPython
