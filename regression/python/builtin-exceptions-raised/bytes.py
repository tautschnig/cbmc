# bytes() and bytearray() of ints, and their exceptions.
assert bytes([1, 2]) == b"\x01\x02"
assert bytes() == b""
assert len(bytes(3)) == 3
assert bytearray([65])[0] == 65
for v in ([256], [-1], [1, 300]):
    try:
        bytes(v)
        assert False
    except ValueError:
        pass
try:
    bytes(-1)
    assert False
except ValueError:
    pass
try:
    bytes("a")
    assert False
except TypeError:
    pass
