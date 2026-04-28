s: str = "hello world"
assert s.startswith("hello")
assert s.endswith("world")
assert not s.startswith("world")
assert not s.endswith("hello")
