# differential2 §12b (UnboundLocalError). Assigning x anywhere in f
# makes x local for the whole function (PLR §4.2.2), so the read
# `y = x` before the assignment `x = 1` is an UnboundLocalError, not a
# read of the module global x.
x = 10


def f() -> int:
    y = x
    x = 1
    return y


f()
