# PLR §3.3.1: iterating a provably non-iterable scalar raises TypeError. The
# for-loop check now covers None (and complex), not just numeric scalars.
# CPython: TypeError; expected: VERIFICATION FAILED.
for x in None:
    pass
