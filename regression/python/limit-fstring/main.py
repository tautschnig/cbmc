# PLR §2.4.3: formatted string literals
x: int = 42
s: str = f"value is {x}"
assert len(s) > 0
