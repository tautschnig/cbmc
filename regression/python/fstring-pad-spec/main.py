# f-string format spec :0N (zero-pad to width N) now
# produces a string of exactly length N (PLR 2.4.3,
# format-spec mini-language §6.1.3).


def pad5(i: int) -> str:
    return f"{i:05}"


def pad3(i: int) -> str:
    return f"{i:03}"


# Length is exactly the width.
assert len(pad5(42)) == 5
assert len(pad5(99999)) == 5
assert len(pad3(7)) == 3


# Combined with literal parts.
def timestamp(h: int, m: int, s: int) -> str:
    return f"{h:02}:{m:02}:{s:02}"


# "HH:MM:SS" = 8 chars.
assert len(timestamp(12, 30, 45)) == 8


# Single FormattedValue with spec (no literal parts).
def pad_only(n: int) -> str:
    return f"{n:04}"


assert len(pad_only(5)) == 4
