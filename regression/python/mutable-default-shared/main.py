# A mutable default is one object, created when the def executes, shared
# by every call that omits the argument (PLR 8.7).
def record(item: str, into: list = []) -> list:
    into.append(item)
    return into


def run() -> int:
    first = record("a")
    second = record("b")
    mine = record("c", [])
    assert first is second or len(first) == 2
    return len(second) * 10 + len(mine)


assert run() == 21
