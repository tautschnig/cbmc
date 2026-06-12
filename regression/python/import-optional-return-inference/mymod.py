class C:
    pass


# Unannotated function returning a class instance OR None. Its inferred
# return type must be Optional (the tagged union), preserving the None
# branch -- not coerced to C (which would make `f(...) is None` unprovable
# and erase the None path: a latent false proof).
def maybe(c):
    if c:
        return C()
    return None
