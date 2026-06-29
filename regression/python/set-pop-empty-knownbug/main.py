# KNOWNBUG (false proof, differential audit 2026-06-29): set().pop() on an empty set -> CPython KeyError; not modelled.
# Desired: VERIFICATION FAILED.
s = set()
v = s.pop()
