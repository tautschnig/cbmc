# CORE (PLR §6.10): `tuple(<non-constant iterable>)` (here tuple(zip(xs, xs)))
# cannot be a fixed-arity python_tuple (unknown length), so it is modelled as a
# nondet python_value (Any). `.index(v)` on that unresolved receiver MAY raise
# (ValueError if v is absent, AttributeError if not a sequence) -- previously it
# returned a silent nondet (a false proof: the tuple of PAIRS never contains the
# int 1, so CPython raises ValueError). CPython raises -> VERIFICATION FAILED.
xs = [5, 8, 3]
t = tuple(zip(xs, xs))
r = t.index(1)
