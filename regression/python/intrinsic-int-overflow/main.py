# PLR: Python ints are unbounded, so 10**20 is 100000000000000000000
# and the assertion holds. By default CBMC models int as signedbv[64]
# (differential2 §2), so 10**20 does not fit and the value used for the
# property is wrong -> the assertion fails. This documents the current
# (default) behaviour: integer overflow turns a PLR-true property into
# a verification failure.
x: int = 10**20
assert x > 0
