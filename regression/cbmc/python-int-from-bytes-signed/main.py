# PLR §4.4.2: int.from_bytes(..., signed=True) interprets the
# input as two's complement. The high bit of the most-
# significant byte (b[0] for big-endian, b[length-1] for
# little-endian) is the sign bit; if set, subtract 2^(length*8)
# from the unsigned interpretation.

# Single-byte negative
assert int.from_bytes(b"\xff", "big", signed=True) == -1
assert int.from_bytes(b"\x80", "big", signed=True) == -128

# Single-byte positive (high bit clear)
assert int.from_bytes(b"\x7f", "big", signed=True) == 127

# signed=False (default) still works
assert int.from_bytes(b"\xff", "big") == 255
