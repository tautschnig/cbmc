# PLR §6.7: shift (<<, >>) and bitwise (&, |, ^) operators accept only int/bool
# operands. A tagged-union (int|str|float) field reaching such an operator as a
# non-int (str or float) raises TypeError. Unlike +,-,*,/ (which also accept
# float), these are int-only, so they need their own obligation. Closes
# b3_diamond_inheritance (str >> int) and b5_multilevel_override (float >> int):
# an inherited mutating method writes str/float into a field a sibling reads via
# `>>`, which mypy/CBMC previously let through (the union tag was not checked).


class Box:
    def __init__(self) -> None:
        self.v: "int | str" = 8

    def mutate(self) -> None:
        self.v = "oops"

    def shift(self) -> int:
        return self.v >> 1  # int-only; str >> int is a TypeError


def main() -> None:
    b = Box()
    b.mutate()       # v is now str (propagates via self)
    b.shift()        # str >> 1 -> TypeError


main()
