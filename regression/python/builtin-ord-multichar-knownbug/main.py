# KNOWNBUG (differential audit r2): ord() of a multi-character string -> CPython
# TypeError (expected a character); the arg-length precondition is not modelled.
# Representative of the builtin-edge cluster (ord/round/sorted/str.encode).
# Desired: VERIFICATION FAILED.
n = ord("ab")
