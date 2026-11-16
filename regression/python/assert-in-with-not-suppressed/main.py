class Loud:
    def __enter__(self) -> "Loud":
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:
        return False


with Loud():
    assert 1 == 2               # not swallowed: the program fails here
