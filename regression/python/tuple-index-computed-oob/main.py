# PLR §6.3.2: a tuple index outside [-len, len) raises IndexError. The tuple's
# arity is statically known, so a NON-constant (computed) index gets the same
# symbolic bounds check that lists have. Found by the property-based random
# fuzzer (21 of 25 false proofs were computed OOB tuple indices).
# CPython: IndexError; expected: VERIFICATION FAILED.
t = (5, 9, 2)
xs = [0, 7, 5]
a = sum(xs)
x = t[a]
