# Representative lift for CHECK-ONLY for-loop bodies
# (--python-smt-containers): `for v in seq: assert P(v)` over a
# symbolic-length sequence is a universally quantified obligation --
# the asserts run ONCE at a nondeterministic in-range index
# (complete at any length, no per-iteration SSA growth; the
# perf-study ex3/ex4 cost wall). Covers list iterables and
# symbolic-bound range().
def fetch() -> list[int]: ...


xs = fetch()
ys = [x * 2 for x in xs]
for y in ys:
    assert y == y
for i in range(0, len(ys)):
    assert ys[i] == ys[i]
assert len(ys) == len(xs)
