class Ctx:
    def __init__(self) -> None:
        pass

    def __enter__(self) -> int:
        return 1

    def __exit__(self, exc_type: int, exc_val: int, exc_tb: int) -> None:
        pass

with Ctx() as c:
    x: int = c
assert x == 1
