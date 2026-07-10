# PLR §6.10: tuple.index(v) with a non-constant-comparable arg (here a python_value
# attribute o.v) that is ABSENT must raise ValueError. The fallback used to
# silently succeed (nondet, no exception) -> a false proof (mutation-oracle). It
# now builds a symbolic `found` predicate and raises when not found.
class C:
    def __init__(self, v):
        self.v = v
o = C(9)
t = (6, 5, 2)
a = t.index(o.v)
