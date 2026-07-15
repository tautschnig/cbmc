# PLR §8.4: TypeError from subscripting a non-subscriptable value is CATCHABLE.
# The handler runs and the program continues -- so no property may fail here
# (the hard-assert form false-alarmed on the real-world corpus's
# `try: ... except Exception` shape). The uncaught case stays definite (see
# typeerror-uncaught-definite).
def f(x):
    try:
        return x[0]
    except Exception:
        return -1


assert f(5) == -1
