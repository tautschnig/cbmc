# Native SMT-String back-end: len() over a case transform of a symbolic string
# must verify (and quickly). Regression for the perf cliff where the result was
# a PYTHON_MAX_STRING_LENGTH-deep str.++ concat whose length the solver had to
# reconstruct position by position -- a len() over it timed out on both cvc5
# and z3. The result is now aliased to a fresh symbol carrying an explicit
# `len(result) == len(input)` hint, so the length query short-circuits.
s = nondet_string(4)
assert len(s.upper()) == len(s)
assert len(s.lower()) == 4
assert len(s.casefold()) == 4
assert len(s.swapcase()) == len(s)
assert len(s.capitalize()) == len(s)
assert len(s.title()) == len(s)
