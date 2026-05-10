# Optional[T] return-type inference: a function that returns
# ClassName() or None is inferred to return python_value_type
# (tagged union). 'is not None' then dispatches precisely on
# the union's tag.


class Match:
    def __init__(self) -> None:
        self.x = 0


def search(found: bool):
    if found:
        return Match()
    return None


# Positive case: search returns Match → 'is not None' is True.
m = search(True)
assert m is not None

# Negative case: search returns None → 'is not None' is False.
m2 = search(False)
assert m2 is None


# Nested conditionals still work.
def find(s: str):
    if len(s) > 0:
        return Match()
    return None


r1 = find("hello")
assert r1 is not None

r2 = find("")
assert r2 is None
