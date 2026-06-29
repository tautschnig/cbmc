# KNOWNBUG (false proof, differential audit 2026-06-29): iterating a concrete non-iterable (for i in 5) -> CPython TypeError; not modelled.
# Desired: VERIFICATION FAILED.
for i in 5:
    pass
