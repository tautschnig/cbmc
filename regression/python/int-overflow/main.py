x: int = 9223372036854775807
y: int = x + 1
# In Python, y == 9223372036854775808 (no overflow)
# With int64, this wraps to -9223372036854775808
assert y > 0
