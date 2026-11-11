# Equal set elements are dropped (PLR 6.2.6) when their equality is only
# known at run time: ints.
def g(i: int):
    t = {i, 3}
    if i != 3:
        assert len(t) == 2
    else:
        assert len(t) == 1


g(int(input()))
