# Companion to unbound-local-error: ordinary local use, a read of a
# module global that is not assigned in the function, and a `global`-
# declared rebind must NOT be flagged as UnboundLocalError.
g = 10


def use_local() -> int:
    a = 1
    b = a + 1
    return b


def read_global() -> int:
    return g + 1


counter = 0


def bump() -> None:
    global counter
    counter = counter + 1


assert use_local() == 2
assert read_global() == 11
bump()
assert counter == 1
