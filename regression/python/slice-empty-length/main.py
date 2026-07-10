# PLR §6.3.3: a slice with start >= stop (`xs[2:1]`) is EMPTY (length 0), not a
# negative `stop - start`. A negative length made the empty slice look non-empty,
# so `xs[2:1] != []` false-proved SUCCESSFUL. Found by the negated value-oracle
# (mutation-oracle) fuzzer mode. Here `r != []` must raise (r is []). CPython:
# AssertionError; expected: VERIFICATION FAILED.
xs = [5, 6]
r = xs[2:1]
assert r != []
