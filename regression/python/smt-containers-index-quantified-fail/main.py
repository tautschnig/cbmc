# twin: index must be the FIRST occurrence
def fetch() -> list[int]: ...


xs = fetch()
if len(xs) > 4 and xs[0] == 9 and xs[1] == 7 and xs[3] == 7:
    assert xs.index(7) == 3
