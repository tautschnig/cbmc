# PLR §2: map() with known function — inline calls
def double(x: int) -> int:
    return x * 2

result: list = list(map(double, [1, 2, 3]))
assert result[0] == 2
assert result[1] == 4
assert result[2] == 6
