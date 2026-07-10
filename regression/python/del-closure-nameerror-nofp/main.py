# No-false-positive: the deleted-flag guard is per-call-site, so a closure call
# BEFORE the del, or after a rebind, must NOT raise; ordinary closures unaffected.
def f1():
    x = 5

    def g():
        return x

    r = g()  # called BEFORE del -> fine
    del x
    return r


def f2():
    x = 5

    def g():
        return x

    del x
    x = 9  # rebind -> defined again
    return g()


def f3():
    x = 5

    def g():
        return x

    return g()  # no del at all


assert f1() == 5 and f2() == 9 and f3() == 5
