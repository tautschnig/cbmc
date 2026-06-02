# PLR / CPython: int("100000") builds a fresh int object each call, and
# 100000 is outside the cached small-int range, so the two objects are
# distinct: `x is not y` is True and the assertion holds. CBMC models
# `is not` as value inequality (differential2 §3/A) and has no
# identity/interning model, so it computes x is not y as x != y ==
# False -> the assertion fails. This KNOWNBUG regex states the
# PLR-correct outcome that the value model cannot produce.
x = int("100000")
y = int("100000")
assert x is not y
