class Span:
    depth: int
    closed: bool

    def __init__(self) -> None:
        self.depth = 0
        self.closed = False

    def __enter__(self) -> "Span":
        self.depth = self.depth + 1
        return self

    def __exit__(self, a, b, c) -> bool:
        self.depth = self.depth - 1
        self.closed = True
        return False


span = Span()
with span as s:
    assert s.depth == 1
    assert span.depth == 1     # s IS span (identity through 'as')
assert span.closed and span.depth == 0


# exception path: __exit__ must run and mutate the REAL manager
class E(Exception):
    pass


sp2 = Span()
hit = 0
try:
    with sp2:
        raise E("boom")
except E:
    hit = 1
assert hit == 1
assert sp2.closed and sp2.depth == 0


# break/continue inside a loop inside the with must NOT run __exit__
span3 = Span()
count = 0
with span3:
    n = 0
    while n < 4:
        n += 1
        if n == 2:
            continue
        if n == 4:
            break
        count += 1
assert count == 2
assert span3.depth == 0
assert span3.closed
