# A method's default arguments must be filled even when the receiver's static
# type is optional/union (e.g. the result of a function annotated "C | None").
# Previously the dispatch on a python_value (tagged-union) receiver built the
# call from provided arguments only, so omitted defaults were nondet-filled by
# the GOTO layer, making any default-driven branch nondet. This is the same
# root cause that blocked re.search(...).group() (no-arg) from being precise.
class C:
    def g(self, a: int = 1, b: int = 2) -> int:
        return a * 10 + b


def make() -> "C | None":
    return C()


r = make()
assert r is not None
assert r.g() == 12       # both defaults applied
assert r.g(5) == 52      # first provided, second defaulted
assert r.g(5, 6) == 56   # both provided
