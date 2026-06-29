# KNOWNBUG (differential audit r2): iterating an instance whose class defines
# neither __iter__ nor __getitem__ -> CPython TypeError ('object is not
# iterable'); not modelled (the for-loop falls through). Desired: FAILED.
# (Nuance for the eventual fix: a class with __getitem__ but no __iter__ IS
# iterable via the old sequence protocol, so the check must allow that.)
class C:
    pass


for x in C():
    pass
