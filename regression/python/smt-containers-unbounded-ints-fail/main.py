def fetch() -> list[int]: ...


xs = fetch()
ys = [x * 1000000000000 for x in xs]
if len(xs) > 2 and xs[1] == 10 ** 15:
    assert ys[1] == 10 ** 27 + 1
