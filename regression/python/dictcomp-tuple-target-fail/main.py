# Vacuity guard: the WRONG dict-comprehension length must be refutable.
def g():
    d = {k: v for k, v in [(1, 2), (3, 4)]}
    assert len(d) == 3


g()
