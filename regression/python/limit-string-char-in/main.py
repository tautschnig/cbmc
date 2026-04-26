# PLR §6.10.2: 'in' for string character membership
# "x in s" should check if character x appears in string s
s: str = "hello"
assert "e" in s
assert "z" not in s
