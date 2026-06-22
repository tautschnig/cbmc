# PLR 3.3.2: stateful data descriptor — __set__ stores per-instance state
# on `obj`, __get__ reads it back. `c.x = 5` then `c.x == 5` requires the
# descriptor methods' `obj` parameter to ALIAS the instance for field
# access. Today `obj` is passed as a tagged python_value whose `obj._v`
# does not alias `c._v` (the byref field path works only when the callee
# is monomorphised to the concrete class, as in the higher-order path).
# So this needs descriptor-method monomorphisation; the __set__ ROUTING +
# side-effect/validation enforcement already work (see data-descriptor-set).
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
