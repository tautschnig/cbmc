# PLR §6.10: string comparison
# String content lost when passed through untyped function params
def identity(s: str) -> str:
    return s
result: str = identity("hello")
assert result == "hello"
