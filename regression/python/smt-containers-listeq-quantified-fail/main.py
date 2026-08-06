def fetch() -> list[int]: ...


xs = fetch()
ys = [x for x in xs]
assert xs != ys
