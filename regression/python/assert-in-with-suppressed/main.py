class Quiet:
    def __enter__(self) -> "Quiet":
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:
        return True                 # swallows the exception


class Loud:
    def __enter__(self) -> "Loud":
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:
        return False


reached = False
with Quiet():
    assert 1 == 2               # raised and swallowed by __exit__
    reached = True
assert not reached              # the rest of the body was skipped
