def fetch() -> list[int]: ...


xs = fetch()
b = all(x > 0 for x in xs)
assert b or not b
if len(xs) > 2 and xs[1] == -5:
    assert not b
    assert any(x < 0 for x in xs)
    assert not all(x >= 0 for x in xs)
d = all(x > 0 for x in xs if x != 0)
