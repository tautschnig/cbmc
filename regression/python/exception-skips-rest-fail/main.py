# The statements after a raise do not run.
g = 0


def f(k: int):
    global g
    x = 10 // k
    g = 1


try:
    f(0)
except ZeroDivisionError:
    assert g == 1
