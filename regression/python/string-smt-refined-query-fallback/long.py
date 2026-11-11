# The SMT encoding spells out at most 64 characters of a refined string, so
# a query over a longer one is unknown: s may end with "q" and this assertion
# fails.
s = nondet_string(70)
assert not s.endswith("q")
