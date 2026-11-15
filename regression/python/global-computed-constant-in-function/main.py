LO = 0x200
SPAN = 0x20
HI = (LO +
      SPAN - 1)


def inside(v: int) -> bool:
    return LO <= v <= HI


assert inside(0x21f)
assert not inside(0x220)
