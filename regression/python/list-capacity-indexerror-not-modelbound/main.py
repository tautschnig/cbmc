# A normal out-of-range index (idx >= length) on a within-capacity list is a
# plain IndexError, NOT a model-bound violation: the access-level capacity
# guard fires only for a VALID index (idx < length) that exceeds capacity, so
# this catches-and-passes rather than spuriously reporting python-model-bound.
def f():
    l = [10, 20, 30, 40, 50]
    try:
        _ = l[100]
        assert False, "IndexError not raised"
    except IndexError:
        pass

f()
