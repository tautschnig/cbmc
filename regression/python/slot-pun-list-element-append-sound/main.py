# CORE (2026-07-10 audit): the append/literal-init paths into a concrete
# `list[int]` element slot now PRESERVE the runtime tag (kept python_value), so a
# mismatched value is NOT punned into int -- the previously-documented false proof
# on these paths is closed. Here `src()` returns a str; CPython: xs[0] is "x",
# isinstance("x", int) is False -> AssertionError, so the correct result is
# VERIFICATION FAILED (the tag is preserved, so the assertion can fail).
# The still-open subscript-store/insert paths of this class are pinned in
# slot-pun-list-element-knownbug. See the coercion-boundary audit table.
def src():
    return "x"


xs: "list[int]" = []
xs.append(src())
y = xs[0]
assert isinstance(y, int)
