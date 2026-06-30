# KNOWNBUG (concrete-slot punning, annotation-laundering class; master inventory
# section A "Coercion-boundary audit", row "List element"). A `list[int]` element
# slot has a CONCRETE element type, so storing an Any/`python_value` of a
# different runtime tag (here a str returned by an unannotated `src()`) PUNS it
# into int, dropping the tag. The value is read back as int, so a misuse does not
# fault. CPython: `xs[0]` is "x" (str), `isinstance("x", int)` is False ->
# AssertionError. cbmc proves the assertion (false proof). Desired: VERIFICATION
# FAILED. Fix = slot-widening (type the element `python_value` when one is
# stored), invasive + perf-costly, deferred. NOTE: requires the concrete `list[int]`
# annotation; without it (`xs = []`) the element is `python_value` and the tag is
# preserved (sound).
def src():
    return "x"


xs: "list[int]" = []
xs.append(src())
y = xs[0]
assert isinstance(y, int)
