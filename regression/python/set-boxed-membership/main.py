def chk(x: int, s) -> bool:
    return x in s


fail = {6}
empty = set()
assert chk(6, fail)
assert not chk(3, fail)
assert not chk(6, empty)
r1 = chk(6, fail if True else set())
r2 = chk(6, fail if False else set())
assert r1
assert not r2


# retry-counter shape (the pyhard false-alarm family): untyped param
def hit(rid: int, remaining) -> bool:
    return rid in remaining


fail_once = {6}
retried = 0
for rid in [3, 6, 9]:
    if hit(rid, fail_once):
        retried += 1
assert retried == 1
