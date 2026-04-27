# PLR §2.4.3: f-string should produce correct content
x: int = 42
s: str = f"x={x}"
assert s == "x=42"
