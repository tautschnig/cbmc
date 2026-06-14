import random

# Native SMT-String back-end: casefold/swapcase (ASCII case mapping) and the
# string-repeat operator on a symbolic subject times a constant count.
assert "ABc".casefold() == "abc"
assert "aBcD".swapcase() == "AbCd"
assert "ab" * 3 == "ababab"

s = random.choice(["xy", "zz"])
if s == "xy":
    assert s.swapcase() == "XY"
    assert s * 2 == "xyxy"
    assert s * 0 == ""
