# Matching-arity, nested, swap, and starred unpacks are unaffected by the arity
# check (only a definite mismatch raises ValueError).
def main() -> None:
    a, b = (1, 2)
    assert a + b == 3
    x, y, z = (1, 2, 3)
    assert x + y + z == 6
    (p, q), r = ((1, 2), 3)
    assert p + q + r == 6
    m, n = 10, 20
    m, n = n, m            # swap
    assert m == 20 and n == 10


main()
