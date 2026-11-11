# int() and float() of constant strings follow CPython: underscores between
# digits, surrounding whitespace and base prefixes are accepted, and an
# invalid literal raises a catchable ValueError.
assert int("1_000") == 1000
assert int(" 7 ") == 7
assert int("-0x1f", 16) == -31
assert int("0b101", 0) == 5
assert int("0_0", 0) == 0
assert float("1_0.5") == 10.5
assert float(" 2.5 ") == 2.5
try:
    n = int("aa")
    assert False
except ValueError:
    pass
try:
    n = int("010", 0)
    assert False
except ValueError:
    pass
try:
    n = int("1__0")
    assert False
except ValueError:
    pass
try:
    f = float("0x1p3")
    assert False
except ValueError:
    pass
