# CORE (PLR §6.10): a tuple passed to a function param flows as a TUPLE-tagged
# python_value; indexing it (`t[0]`) must NOT raise a spurious "not
# subscriptable" TypeError (a tuple IS subscriptable). TUPLE was missing from the
# python_value subscriptable-tag set -- a false alarm. The element value is a
# sound nondet (precise boxed-tuple extraction is a follow-up), so no assertion
# on it here; this guards that the subscript no longer spuriously faults.
def f(t):
    return t[0]


f((4, 5))
assert True
