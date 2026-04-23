# Crash: string set with 'in' operator causes type mismatch
s: set[str] = {"foo", "bar"}
assert "foo" in s
