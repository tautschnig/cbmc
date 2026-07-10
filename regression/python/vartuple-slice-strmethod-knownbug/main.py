# KNOWNBUG (false proof, ty-010) -- RETURN-ANNOTATION LAUNDERING (same family as
# the 004/007 Any-laundering cases, caught under --python-check-annotations,
# default-declined). `f` is annotated `-> str` but returns `(1,2,3)[1:]` (a tuple),
# so CPython's `f().upper()` raises AttributeError. By DEFAULT the frontend TRUSTS
# the `-> str` annotation -> f() is typed str -> `.upper()` is accepted (the false
# proof). Under --python-check-annotations the return-annotation mismatch IS
# flagged. Default-on enforcement was measured ~1.25% benign-FP and declined.
# Desired: VERIFICATION FAILED.
#
# NOTE (2026-07-10 audit): the slice is on a DIRECT literal `(1,2,3)[1:]` so the
# laundering path is exercised cleanly. The earlier form used a `tuple[...]`-typed
# PARAMETER (`def f(t: tuple[int, ...]): return t[1:]`), but a tuple-ANNOTATED
# parameter is currently treated as NOT SUBSCRIPTABLE (a separate sound false
# ALARM -- see the "tuple-annotated parameter subscript" row in inventory B), which
# raised a spurious TypeError on `t[1:]` and MASKED the laundering this test
# documents.
def f() -> str:
    return (1, 2, 3)[1:]


f().upper()
