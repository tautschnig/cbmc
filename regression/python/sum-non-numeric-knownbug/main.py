# KNOWNBUG (false proof, differential audit 2026-06-29): sum() of non-numeric elements (sum(["a","b"])) -> CPython TypeError (0 + str); not modelled.
# Desired: VERIFICATION FAILED.
s = sum(["a", "b"])
