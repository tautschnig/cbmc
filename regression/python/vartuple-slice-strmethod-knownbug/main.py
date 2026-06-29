# KNOWNBUG (false proof, ty-010) -- RETURN-ANNOTATION LAUNDERING (same family as
# the 004/007 Any-laundering cases, caught under --python-check-annotations,
# default-declined). `f` is annotated `-> str` but returns `t[1:]` (a tuple), so
# CPython's `f(...).upper()` raises AttributeError. By DEFAULT the frontend
# TRUSTS the `-> str` annotation -> f(...) is typed str -> `.upper()` is accepted
# (the false proof). Under --python-check-annotations the return-annotation
# mismatch IS flagged (FAILURE on `return t[1:]`), but default-on enforcement was
# measured ~1.25% benign-FP and declined. This is NOT a variable-length-tuple
# modelling gap: a fixed-tuple slice is modelled exactly (see the `tuple-slice`
# test), and the concrete-receiver `(1,2,3)[1:].upper()` IS caught. Desired:
# VERIFICATION FAILED.
def f(t: "tuple[int, ...]") -> str:
    return t[1:]


f((1, 2, 3)).upper()
