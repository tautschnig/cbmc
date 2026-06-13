def shrink(x):
    # x is unannotated -> Any. `pop` is shared across list/dict/set, so it is
    # dispatched at runtime on python_value.__tag; for a list it removes and
    # returns the last element, and the length change propagates to the caller.
    return x.pop()


a = [1, 2, 3]
v = shrink(a)
assert len(a) == 2
assert v == 3
