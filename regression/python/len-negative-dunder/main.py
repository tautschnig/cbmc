# PLR 3.3.1: len() of an instance whose __len__ provably returns a negative
# integer constant raises ValueError ('__len__() should return >= 0').
class C:
    def __len__(self) -> int:
        return -1


n = len(C())
