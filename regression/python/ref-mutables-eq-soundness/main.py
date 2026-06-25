# Reference-semantics equality soundness (--python-ref-mutables): two DISTINCT
# nested lists holding equal values must NOT be proved unequal. The bitwise
# struct comparison used to compare element heap pointers, which wrongly proved
# `a != b` (a false proof). Equality of reference elements is now sound: it is
# nondet for distinct references (deep structural compare is intractable, see
# spike doc §12), so neither `==` nor `!=` is provable here -> the assertion of
# inequality must FAIL (cannot be proved).
a = [[1]]
b = [[1]]
assert a != b
