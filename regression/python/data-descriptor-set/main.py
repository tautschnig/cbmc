# PLR 3.3.2: a DATA descriptor (defines __set__) intercepts attribute
# assignment. `c.x = v` routes to Bounded.__set__(desc, c, v), so the
# descriptor's invariant (value >= 0) is enforced on assignment.
class Bounded:
    def __set__(self, obj, value: int) -> None:
        assert value >= 0
        obj._v = value

    def __get__(self, obj, objtype=None) -> int:
        return obj._v


class C:
    x = Bounded()

    def __init__(self):
        self._v = 0


c = C()
c.x = 5  # __set__ runs; its assert (5 >= 0) holds
