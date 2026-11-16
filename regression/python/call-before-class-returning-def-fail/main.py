import dataclasses


@dataclasses.dataclass
class Word:
    low: int
    mid: int


def low_of(v: int) -> int:
    d = split(v)
    return d.low


def split(v: int) -> Word:
    return Word(low=v & 0xff, mid=(v >> 8) & 0x3ffff)


assert low_of(0x1234) == 0x12
