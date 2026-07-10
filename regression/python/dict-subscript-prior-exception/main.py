# PLR §8.3 first-exception-wins: in `d[t.index(a)]`, the key expression
# t.index(a) raises ValueError (a=2 absent from t). The dict subscript's
# missing-key KeyError must NOT overwrite that pending ValueError with KeyError
# -- else an `except KeyError` wrongly catches it (a false proof found by the
# mutation-oracle). The ValueError propagates uncaught -> verification FAILS.
a = 2
t = (0, 3, 3)
d = {0: 1}
try:
    a = d[t.index(a)]
except KeyError:
    a = 0
