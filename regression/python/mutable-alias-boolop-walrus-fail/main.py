# PLR §6.11/§6.12: `m = l or [9]` (BoolOp, truthy path) and `(m := l)`
# (walrus) bind m to the SAME OBJECT as the mutable Name l; a value copy
# was a false proof -- a later `m.append(x)` did not propagate to l
# (ESBMC github_5955_alias_{boolop,walrus}_fail). The conditional-alias
# shapes are recorded as extraction aliases, so a mutation through m
# havocs l (sound); the stale `count == 0` can no longer be proven.
def boolop() -> None:
    l = [1, 2]
    m = l or [9]
    m.append(3)
    assert l.count(3) == 0


def walrus() -> None:
    l = [1, 2]
    if (m := l):
        m.append(3)
    assert l.count(3) == 0


boolop()
walrus()
