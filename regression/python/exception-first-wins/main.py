# PLR §8.3: the FIRST exception raised on a path wins. `xs.index(xs[4])`: the
# argument `xs[4]` raises IndexError (xs has length 1) BEFORE index() runs, so an
# `except ValueError` must NOT catch it. Previously index()'s not-found ValueError
# (over the nondet OOB read) overwrote the pending IndexError type, so the wrong
# handler caught it -> false proof. Found by the value-oracle fuzzer. The
# IndexError must propagate uncaught. CPython: IndexError; expected: FAILED.
xs = [2]
try:
    a = xs.index(xs[4])
except ValueError:
    a = 2
r = a
