# KNOWNBUG (concrete-slot punning, annotation-laundering class; master inventory
# section A "Coercion-boundary audit", row "List element"). A `list[int]` element
# slot has a CONCRETE element type, so storing an Any/`python_value` of a
# different runtime tag (here a str returned by an unannotated `src()`) via a
# SUBSCRIPT STORE puns it into int, dropping the tag. The value is read back as
# int, so a misuse does not fault. CPython: `xs[0]` is "x" (str),
# `isinstance("x", int)` is False -> AssertionError. cbmc proves the assertion
# (false proof). Desired: VERIFICATION FAILED. Fix = slot-widening (type the
# element `python_value` when one is stored), invasive + perf-costly, deferred.
#
# NOTE (2026-07-10 audit): the `append`/literal-init paths of THIS class are now
# SOUND (the element is kept `python_value`, tag preserved) -- see the CORE
# `slot-pun-list-element-append-sound` guard. Only the SUBSCRIPT STORE (`xs[0]=`)
# and `insert` paths still pun, so this KNOWNBUG now exercises the subscript store,
# which remains a real false proof.
def src():
    return "x"


xs: "list[int]" = [0]
xs[0] = src()
y = xs[0]
assert isinstance(y, int)
