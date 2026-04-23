# Case F: exception handler assigns different type
def compute(v: int) -> int:
    if v < 0:
        raise ValueError("negative")
    return v * 2

try:
    x = compute(5)
except ValueError:
    x = -1

assert x == 10
