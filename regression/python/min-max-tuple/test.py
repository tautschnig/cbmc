# Regression: PLR §6.10.2 treats tuples and lists uniformly for
# min()/max(). The frontend used to only fold the list form, so
# inline tuple arguments hit the generic-call fallback.
assert min((3, 1, 4, 2)) == 1
assert max((3, 1, 4, 2)) == 4
assert min((1.5, 2.5, 0.5)) == 0.5
assert max((1, 2.5, 3)) == 3
