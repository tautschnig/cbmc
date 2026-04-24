# Limitation: strings bounded to PYTHON_MAX_STRING_LENGTH (256)
# This test verifies that strings up to the bound work correctly
s: str = "a"
t: str = s + s + s + s + s  # 5 chars
assert len(t) == 5
# Strings longer than 256 chars would be truncated
