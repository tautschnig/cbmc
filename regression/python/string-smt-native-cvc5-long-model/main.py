# The result of a stub function is an unbounded string. cvc5 finds a model
# with a string of 2^46 characters, which it cannot print: it prints witness
# terms instead of values. CBMC then asks again with --strings-fmf, which
# finds a short counterexample.
def ns() -> str: ...


s = ns()
assert len(s) <= 64
