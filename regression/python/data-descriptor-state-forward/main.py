# PLR 3.3.2 (now SUPPORTED): stateful data descriptor where the descriptor class (Pos) is
# defined BEFORE the field-owning class (C). When Pos.__get__/__set__
# bodies convert, no registered class yet declares `_v` (C and its dynamic
# `_v` field are registered later), so `obj._v` inside the descriptor is resolved by the 1a-bis re-pass
# (which re-converts the descriptor methods after C's struct is registered). (Contrast data-descriptor-stateful, where
# the field-owning base class is registered first and this verifies.)
# Fix: register all class struct fields before converting any method body
# (a conversion-ordering change); see the descriptors plan.
class Pos:
    def __set__(self, obj, value: int) -> None:
        obj._v = value

    def __get__(self, obj, objtype=None) -> int:
        return obj._v


class C:
    x = Pos()

    def __init__(self):
        self._v = 0


c = C()
c.x = 5
assert c.x == 5
