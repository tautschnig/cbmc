# KNOWNBUG (false PROOF residual): an EXPLICIT iterator over a bounded
# range should raise StopIteration when exhausted. `iter(range(2))` returns
# the underlying identity with NO consumption cursor, so `next` re-reads and
# the third `next` does not raise. DESIRED: VERIFICATION FAILED (CPython
# raises StopIteration on the 3rd next). CURRENT: VERIFICATION SUCCESSFUL.
# Needs the generator-cursor machinery extended to explicit iterators
# (see plan section 1). The for-loop and Name-alias cursors are already
# precise. When fixed -> promote to CORE.
it = iter(range(2))
a = next(it)
b = next(it)
c = next(it)  # should raise StopIteration
assert c == c
