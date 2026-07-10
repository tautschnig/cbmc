# KNOWNBUG (concrete-slot punning, annotation-laundering class; master inventory
# section A "Coercion-boundary audit", row "List element"). A `list[int]` element
# slot has a CONCRETE element type, so storing an Any/`python_value` of a
# different runtime tag (here a str returned by an unannotated `src()`) via a
# SUBSCRIPT STORE puns it into int, dropping the tag. The value is read back as
# int, so a misuse does not fault. CPython: `xs[0]` is "x" (str),
# `isinstance("x", int)` is False -> AssertionError. cbmc proves the assertion
# (false proof). Desired: VERIFICATION FAILED.
#
# NOTE (2026-07-10): the append/extend/INSERT paths into an EMPTY-init list are
# now SOUND -- the element-inference pre-pass widens the element to python_value
# when a mismatched/uninferable value is stored (see the CORE
# slot-pun-list-element-append-sound / -insert-sound guards). The pre-pass only
# fires at the empty-list CREATION site, so the SUBSCRIPT STORE (`xs[i]=`) and
# stores into a NON-empty-init list still pun -- this KNOWNBUG exercises the
# subscript store, which remains a real false proof. Fix = extend the inference
# to subscript-store / non-empty-init, or slot-widening.
def src():
    return "x"


xs: list[int] = [0]
xs[0] = src()
y = xs[0]
assert isinstance(y, int)
