# differential2 §12c: comprehensions over computed/runtime iterables.
# A list comprehension whose iterable is a CALL returning a runtime
# list, and a set comprehension over a runtime list parameter, are both
# lowered to a real loop (SetComp shares the ListComp lowering) instead
# of being dropped or over-approximated.
def src(n: int) -> list:
    r = []
    for i in range(n):
        r.append(i)
    return r


def via_call(n: int) -> int:
    ys = [x * 2 for x in src(n)]
    return len(ys)


def via_set(xs: list) -> int:
    s = {x * 2 for x in xs}
    return len(s)


assert via_call(4) == 4
assert via_set([1, 2, 3]) == 3
