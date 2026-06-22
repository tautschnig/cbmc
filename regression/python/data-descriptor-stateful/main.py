# PLR 3.3.2: a STATEFUL data descriptor. __set__ stores per-instance state
# on the instance (obj._v), __get__ reads it back. Works when the
# field-owning class (Holder, providing _v) is registered before the
# descriptor method bodies convert, so `obj._v` inside __get__/__set__
# resolves to the real field (rather than baking to nondet).
class Holder:
    def __init__(self):
        self._v = 0


class Pos:
    def __set__(self, obj, value: int) -> None:
        obj._v = value

    def __get__(self, obj, objtype=None) -> int:
        return obj._v


class C(Holder):
    x = Pos()


c = C()
c.x = 5
assert c.x == 5  # __set__ stored obj._v=5; __get__ reads it back
