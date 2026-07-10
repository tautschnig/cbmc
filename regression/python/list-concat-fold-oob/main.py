# The concat-fold tracks length through a variable reassignment `xs = xs + [..]`
# (homogeneous), so an out-of-range index on the concatenated list raises
# IndexError. CPython: IndexError.
xs = [1, 2]
xs = xs + [3]
x = xs[9]
