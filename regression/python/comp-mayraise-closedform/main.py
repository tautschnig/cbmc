# Representative+lift (perf-study 5b): a comprehension whose BODY may
# raise still gets the exact closed form -- its checks run once at a
# nondeterministic representative index. Previously such bodies fell
# to the loop lowering, which under --python-smt-containers HUNG
# symex (symbolic trip count) with no property and no formula.
def fetch() -> list[int]: ...


xs = fetch()
ys = [x + 1 for x in xs]
zs = [y - 1 for y in ys]
assert len(zs) == len(xs)
if len(xs) > 2:
    assert zs[1] == xs[1]
