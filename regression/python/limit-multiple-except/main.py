# PLR §8.4: try with multiple except handlers
result: int = 0
try:
    raise IndexError("test")
except ValueError:
    result = 1
except IndexError:
    result = 2
assert result == 2
