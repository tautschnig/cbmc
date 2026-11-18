# A nested function mutating a list and a dict of its enclosing function.
def outer() -> int:
    calls = []
    seen = {}
    def note(k: str) -> int:
        calls.append(1)
        seen[k] = len(calls)
        return len(calls)
    def twice() -> int:
        return note("a") + note("b")
    assert twice() == 3
    return len(calls) * 10 + seen["b"]


assert outer() == 22
assert outer() == 22
