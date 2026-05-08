# Exercise the Python front-end diagnostics in their default (quiet)
# mode. Each line below would emit an 'over-approximation' log at
# debug level that --python-strict-warnings would promote to a
# warning, but neither form must fire in the default mode.
def f(s):
    # Slice over an opaque value: falls through to the Slice nondet
    # handler.
    return s[1:3]


def g(x):
    # Attribute access on an opaque base: goes through the
    # over-approximating attribute handler.
    return x.something


# A trivial assertion so the test has a property to verify.
assert True
