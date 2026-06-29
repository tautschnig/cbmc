# --python-check-annotations: a value whose type is incompatible with a list's
# CONCRETE element annotation, appended via a CALL argument (xs.append(src())),
# is an annotation mismatch (ty-007 laundering). The call-arg case was
# previously skipped (to avoid double-evaluating a side-effecting arg); it is
# now checked by reading the callee's static return type. Opt-in (legal at
# runtime; the TypeError arises on a later use).
def src() -> str:
    return "s"


xs: list[int] = []
xs.append(src())
