# twin: membership must not over-approximate either
def hit(x: int, s) -> bool:
    return x in s


assert hit(3, {6})
