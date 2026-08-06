def fetch() -> list[int]: ...


xs = fetch()
ys = [i for i, v in enumerate(xs)]
if len(xs) > 3:
    assert ys[2] == 3
