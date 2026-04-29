s: str = "hello world"
parts: list = s.split(" ")
result: str = "-".join(parts)
assert result == "hello-world"
