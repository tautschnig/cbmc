# A comprehension filter that does not constant-evaluate must not be
# treated as unconditionally true (the old unroll produced
# definite-wrong list contents in both directions).
def f():
    xs = ['aaa', 'emrX', 'bemr']
    ys = [x for x in xs if 'emr' in x]
    assert len(ys) == 2


f()
