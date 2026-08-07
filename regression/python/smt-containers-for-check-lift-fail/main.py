# twin + vacuity probe: a property false for SOME element value must
# fail -- the representative obligation must not prove vacuously.
def fetch() -> list[int]: ...


xs = fetch()
ys = [x * 2 for x in xs]
for y in ys:
    assert y == 4
