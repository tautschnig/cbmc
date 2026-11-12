# str() of an int of more than 4300 digits raises ValueError (CPython 3.11),
# and ints beyond int64 convert exactly.
n = 2 ** 64
assert str(n) == "18446744073709551616"
assert f"{n}" == "18446744073709551616"
assert len(f"{n:>3}") == 20
assert len(str(10 ** 4299)) == 4300
try:
    str(10 ** 4300)
    assert False
except ValueError:
    pass
try:
    f"{-(10 ** 4300)}"
    assert False
except ValueError:
    pass
