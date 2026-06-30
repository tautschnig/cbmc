# KNOWNBUG: applying a non-callable decorator (@d where d=5) raises TypeError in
# CPython ('int object is not callable'). The frontend does not model general
# decorator application (@d -> f = d(f)), so it is silently accepted.
# Desired: VERIFICATION FAILED. Needs decorator-application modelling.
d = 5


@d
def f() -> None:
    pass
