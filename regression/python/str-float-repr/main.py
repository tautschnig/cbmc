# CPython float repr (py_float_repr): shortest round-trip digits with
# Python's fixed/scientific threshold and the mandatory decimal point.
# The previous 6-digit ostream fold produced WRONG constants across
# str() / "{}".format() / f-strings -- str(1.0)="1", str(0.1234567)=
# "0.123457" -- a false-proof channel (ESBMC str_float_* / str_format_*).
assert str(1.0) == "1.0"
assert str(-1.0) == "-1.0"
assert str(0.1234567) == "0.1234567"
assert str(1e15) == "1000000000000000.0"
assert str(0.5) == "0.5"

assert "{}".format(1.0) == "1.0"
assert "{}".format(-1.0) == "-1.0"
assert "{}".format(1.23456789) == "1.23456789"

assert f"{1.0}" == "1.0"
assert f"{0.1234567}" == "0.1234567"

# presentation specs and integers are unaffected
assert "{:.2f}".format(3.14159) == "3.14"
assert "{}".format(5) == "5"
assert str(255) == "255"
