# CORE (2026-07-10): a subscript-store `xs[i] = v` of a value whose runtime tag
# is incompatible with a list's element type no longer PUNS it into the concrete
# type. The element-inference pre-pass detects the mismatched store (`xs[0] =
# src()`, src returns str, xs is list[int]) and widens the element to
# python_value at the list's creation site, so the tag is preserved and a misuse
# faults. CPython: xs[0] is "x" (str), isinstance("x", int) is False ->
# AssertionError, so the correct result is VERIFICATION FAILED. A correctly-typed
# subscript-store keeps int (see slot-pun-list-element-subscript-nofp). This was
# the last open list-element punning path (append/extend/insert closed earlier);
# the annotated attribute field (slot-pun-attr-field-knownbug) remains.
def src():
    return "x"


xs: list[int] = [0]
xs[0] = src()
y = xs[0]
assert isinstance(y, int)
