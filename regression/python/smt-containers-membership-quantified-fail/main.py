def fetch() -> list[int]: ...


xs = fetch()
if len(xs) > 3 and xs[2] == 7:
    assert 7 not in xs
