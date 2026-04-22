def risky(x: int) -> int:
    if x < 0:
        raise ValueError("negative")
    return x

try:
    result: int = risky(-1)
except ValueError:
    result: int = 0

assert result == 0
