# KNOWNBUG: unpacking a non-iterable class instance (a, b = C()) raises TypeError
# in CPython. The assign-unpack handler is a separate iteration site not yet
# covered by the iteration-protocol-missing check (for-loop + comprehension are).
# Desired: VERIFICATION FAILED.
class C:
    pass


a, b = C()
