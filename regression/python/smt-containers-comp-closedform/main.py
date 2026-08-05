# Closed-form MAP comprehensions (--python-smt-containers): a
# filter-free single-generator comprehension with a pure element
# expression lowers to array_comprehension_exprt (lambda/forall in
# smt2) instead of a loop -- EXACT at any SYMBOLIC length, no
# unwinding, no truncation. This test runs WITH unwinding assertions:
# the old loop lowering fails .unwind.0 here.
def fetch() -> list: ...


def n_of() -> int: ...


xs = fetch()
ys = [x * 2 + 1 for x in xs]
assert len(ys) == len(xs)
if len(xs) > 3:
    assert ys[2] == xs[2] * 2 + 1

# composition of closed forms
zs = [y - 1 for y in ys]
if len(xs) > 5:
    assert zs[4] == xs[4] * 2

# concrete lists still fold at symex time (simplifier substitution)
cs = [1, 2, 3]
ds = [c * 10 for c in cs]
assert ds[0] == 10 and ds[1] == 20 and ds[2] == 30
assert len(ds) == 3

# range with symbolic n
n = n_of()
if 0 <= n:
    rs = [i * 3 for i in range(n)]
    assert len(rs) == n

# identity map over boxed (nested-container) elements
ws = [d for d in xs]
assert len(ws) == len(xs)
