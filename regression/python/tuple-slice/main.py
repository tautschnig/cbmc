# PLR §6.3.3: slicing a tuple returns a TUPLE. A fixed-length tuple slice has a
# statically-known result; it was previously mis-modelled (fell through to the
# single-index path, so `len`/indexing of the slice were wrong).
def main() -> None:
    t = (1, 2, 3, 4)
    assert len(t[1:]) == 3
    assert t[1:][0] == 2
    assert t[1:3] == (2, 3) or (len(t[1:3]) == 2 and t[1:3][0] == 2)
    assert len(t[-2:]) == 2 and t[-2:][0] == 3
    assert t[::-1][0] == 4
    assert len(t[10:]) == 0


main()
