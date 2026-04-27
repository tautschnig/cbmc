# PLR §8.4: try with multiple except handlers
result: int = 0
try:
    x: int = 1 // 0
except ZeroDivisionError:
    result = 1
except ValueError:
    result = 2
assert result == 1
