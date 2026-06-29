# KNOWNBUG (differential audit r2): __len__ returning a non-int (or negative)
# -> CPython TypeError/ValueError at len(); the __len__ return is not validated.
# Desired: VERIFICATION FAILED.
class C:
    def __len__(self) -> int:
        return -1


n = len(C())
