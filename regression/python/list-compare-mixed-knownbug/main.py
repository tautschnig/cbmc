# KNOWNBUG (false proof, differential audit 2026-06-29): ordering comparison of lists with type-incompatible elements ([1,2] < [1,'a']) -> CPython TypeError; not modelled.
# Desired: VERIFICATION FAILED.
r = [1, 2] < [1, "a"]
