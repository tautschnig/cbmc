# @c_intrinsic with str args — verify that Python string literals
# handed to a C function arrive as a persistent, NUL-terminated
# char* buffer that CBMC can safely dereference. Regressing the
# earlier "dead object in *name" / "deallocated dynamic object"
# failures we saw when the literal's backing array had only stack
# lifetime.

def c_intrinsic(name):
    def deco(fn):
        return fn
    return deco


@c_intrinsic("getenv")
def getenv(name: str) -> str:
    pass


# Just calling getenv with a literal should verify without any
# pointer-dereference failures — getenv's builtin body dereferences
# the name argument, so the backing storage must be alive and
# NUL-terminated.
v = getenv("HOME")
