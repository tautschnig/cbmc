def check(x: int) -> None:
    if x < 0:
        raise ValueError("must be non-negative")
