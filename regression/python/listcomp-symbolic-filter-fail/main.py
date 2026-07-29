# Vacuity guard for the symbolic-filter fix: the WRONG length must be
# refutable (the old unroll proved len == 3 for a 2-element result).
def g():
    xs = ['aaa', 'emrX', 'bemr']
    ys = [x for x in xs if 'emr' in x]
    assert len(ys) == 3


g()
