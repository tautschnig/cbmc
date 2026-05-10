"""
Verification model of the `struct` module.

Binary pack/unpack. Every call returns a nondet bytes object
or nondet tuple of values of appropriate lengths. Format-string
introspection returns the minimum-length values CPython
specifies.
"""


class error(Exception):
    pass


# Format-size table for calcsize.
_SIZES = {
    "x": 1, "c": 1, "b": 1, "B": 1,
    "?": 1, "h": 2, "H": 2, "i": 4, "I": 4,
    "l": 4, "L": 4, "q": 8, "Q": 8,
    "n": 8, "N": 8, "e": 2, "f": 4, "d": 8,
    "s": 1, "p": 1, "P": 8,
}


def calcsize(fmt: str) -> int:
    # Strip leading byte-order marker.
    fmt = fmt.lstrip("@<=>!")
    total = 0
    digits = ""
    for ch in fmt:
        if ch.isdigit():
            digits = digits + ch
            continue
        if ch in _SIZES:
            mult = int(digits) if digits else 1
            total = total + mult * _SIZES[ch]
        digits = ""
    return total


def pack(fmt: str, *values) -> bytes:
    return b"\x00" * calcsize(fmt)


def pack_into(fmt: str, buffer, offset: int, *values):
    return None


def unpack(fmt: str, buffer) -> tuple:
    # Return a tuple of zeros of the expected length.
    count = 0
    digits = ""
    out = []
    f = fmt.lstrip("@<=>!")
    for ch in f:
        if ch.isdigit():
            digits = digits + ch
            continue
        if ch in _SIZES and ch != "x" and ch != "s" and ch != "p":
            mult = int(digits) if digits else 1
            for _ in range(mult):
                if ch == "?":
                    out.append(False)
                elif ch in "fde":
                    out.append(0.0)
                else:
                    out.append(0)
        elif ch == "s" or ch == "p":
            out.append(b"")
        digits = ""
    return tuple(out)


def unpack_from(fmt: str, buffer, offset: int = 0) -> tuple:
    return unpack(fmt, buffer)


def iter_unpack(fmt: str, buffer):
    return iter([])


class Struct:
    def __init__(self, fmt: str):
        self.format = fmt
        self.size = calcsize(fmt)

    def pack(self, *values) -> bytes:
        return pack(self.format, *values)

    def pack_into(self, buffer, offset: int, *values):
        return pack_into(self.format, buffer, offset, *values)

    def unpack(self, buffer) -> tuple:
        return unpack(self.format, buffer)

    def unpack_from(self, buffer, offset: int = 0) -> tuple:
        return unpack_from(self.format, buffer, offset)

    def iter_unpack(self, buffer):
        return iter_unpack(self.format, buffer)
