# PLR §6.7: a strictly-numeric binary op (-, /, //, **) on TWO tagged-union
# operands raises TypeError when either is non-numeric. Here a method retags both
# union fields to str, then `a - b` is str - str. The one-union/one-concrete case
# already emitted the obligation; the BOTH-union case did not (a12 false proof).
# Add/Mod/bitwise are excluded (str/list concat, str %-formatting, set ops are
# valid), so only the strictly-numeric ops flag both-union operands.
class Pair:
    def __init__(self) -> None:
        self.a: "int | str" = 1
        self.b: "int | str" = 2

    def corrupt_both(self) -> None:
        self.a = "x"
        self.b = "y"


def main() -> None:
    p = Pair()
    if isinstance(p.a, int) and isinstance(p.b, int):
        p.corrupt_both()
        result: int = p.a - p.b   # str - str -> TypeError


main()
