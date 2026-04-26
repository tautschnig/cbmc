# PLR §7.6: return with tuple
def swap(a: int, b: int) -> tuple:
    return b, a

x, y = swap(1, 2)
assert x == 2
assert y == 1
