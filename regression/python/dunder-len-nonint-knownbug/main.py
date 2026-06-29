# KNOWNBUG: __len__ returning a NEGATIVE int -> CPython ValueError ('__len__()
# should return >= 0'). This is a value check (not type): a sound check without
# value tracking would false-positive on symbolic-but-nonneg returns, so it is
# left pinned. The non-int-TYPE case is fixed (see dunder-len-nonint-type).
# Desired: VERIFICATION FAILED.
class C:
    def __len__(self) -> int:
        return -1


n = len(C())
