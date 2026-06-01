# PLR 8.5: a context manager whose __exit__ returns a truthy value
# suppresses an exception raised in the with-body; one returning a
# falsy value lets it propagate.


class Suppress:
    def __enter__(self) -> "Suppress":
        return self

    def __exit__(self, et, ev, tb) -> bool:
        return True


class NoSuppress:
    def __enter__(self) -> "NoSuppress":
        return self

    def __exit__(self, et, ev, tb) -> bool:
        return False


def f() -> int:
    with Suppress():
        raise ValueError("boom")
    return 42


# Exception suppressed -> control resumes after the with.
assert f() == 42
