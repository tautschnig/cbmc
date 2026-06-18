# os.remove can raise OSError; under --python-raising-ops-check the
# may-raise is modeled so the assert-True-on-success path is not proved.
from os import remove


def delete(path: str) -> bool:
    try:
        remove(path)
        return True
    except OSError:
        return False


def f() -> None:
    r = delete("/tmp/x")
    assert r == True  # not guaranteed: remove may raise -> r == False


f()
