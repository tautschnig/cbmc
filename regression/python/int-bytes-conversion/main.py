# PLR §4.4.2: int.from_bytes(b, byteorder, *, signed=False)
#             int.to_bytes(self, length, byteorder, *, signed=False)
# Class-method dispatch on the int built-in, modelled
# directly in the converter (no library shim) so the
# byte-order constants can be folded at conversion time.

def to_bytes_big() -> None:
    b = int.to_bytes(255, 2, "big")
    assert b[0] == 0
    assert b[1] == 255


def to_bytes_little() -> None:
    b = int.to_bytes(255, 2, "little")
    assert b[0] == 255
    assert b[1] == 0


def from_bytes_big() -> None:
    bs = b"\x00\x10"
    assert int.from_bytes(bs, "big") == 16


def from_bytes_little() -> None:
    bs = b"\x00\x10"
    assert int.from_bytes(bs, "little") == 4096


def from_bytes_long_big() -> None:
    bs = b"\x01\x02\x03\x04"
    # 0x01020304 = 16909060
    assert int.from_bytes(bs, "big") == 16909060


def round_trip_big() -> None:
    b = int.to_bytes(0x12345678, 4, "big")
    assert int.from_bytes(b, "big") == 0x12345678


def round_trip_little() -> None:
    b = int.to_bytes(0x12345678, 4, "little")
    assert int.from_bytes(b, "little") == 0x12345678


to_bytes_big()
to_bytes_little()
from_bytes_big()
from_bytes_little()
from_bytes_long_big()
round_trip_big()
round_trip_little()
