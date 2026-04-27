# PLR §2.4.5: map(func, iterable)
def double(x: int) -> int:
    return x * 2
result = list(map(double, [1, 2, 3]))
assert result[0] == 2
