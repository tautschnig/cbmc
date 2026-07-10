# CORE (2026-07-10): appending/inserting a value whose runtime tag is
# incompatible with a list's CONCRETE element annotation (`list[int]`) now
# WIDENS the element to python_value (Any-dominance) at the empty-list creation
# site (the element-inference pre-pass), so the tag is PRESERVED -- the previous
# false proof (the mismatched value punned into int) is closed. src() returns a
# str; CPython: xs[0] is "x", isinstance("x", int) is False -> AssertionError, so
# the correct result is VERIFICATION FAILED. A correctly-typed store keeps int
# (see -nofp). The subscript-store / non-empty-init paths of this class remain
# open (slot-pun-list-element-knownbug).
def src():
    return "x"


xs: list[int] = []
xs.append(src())
assert isinstance(xs[0], int)
