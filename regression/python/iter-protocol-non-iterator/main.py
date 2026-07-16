# PLR §3.3.1 iterator protocol: __iter__ must return an ITERATOR. Returning
# a plain list raises "TypeError: iter() returned non-iterator of type
# 'list'" -- and a broken __iter__ SHADOWS the legacy __getitem__ protocol
# (CPython-verified). Both the concrete-instance path and the pv-CLASS
# dispatch previously iterated the returned list silently (false proofs).
# The classifier is syntactic (VALID: iter()/reversed()/map()/filter()/
# zip()/enumerate(), generator bodies, genexps, `return self` + __next__;
# INVALID: container literals, constants, bare/None returns, `return self`
# without __next__; anything else UNKNOWN -> unflagged).
class Bad:
    def __iter__(self) -> list:
        return []

    def __getitem__(self, i: int) -> int:
        return i


b = Bad()
for c in b:  # TypeError -- __getitem__ does NOT rescue a broken __iter__
    pass
assert True
