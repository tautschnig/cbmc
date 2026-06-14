class C:
    pass


def f(c):
    return C() if c else None


# Soundness guard: f(0) returns None, so this is FALSE and must NOT verify.
# Before the IfExp return-type fix, the None branch was coerced to the class
# type and this wrongly verified SUCCESSFUL (None was unreachable in the model).
x = f(0)
assert x is not None
