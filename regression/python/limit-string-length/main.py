# Limitation: strings bounded to 256 characters
s: str = "a" * 300
assert len(s) == 300
