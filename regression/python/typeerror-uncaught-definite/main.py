# Companion: with NO enclosing handler (or one that does not cover TypeError),
# subscripting a non-subscriptable value remains a definite failure.
def f(x):
    try:
        return x[0]
    except KeyError:
        return -1


f(5)
