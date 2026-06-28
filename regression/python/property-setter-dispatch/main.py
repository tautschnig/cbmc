# PLR §3.3.2: a @property is a data descriptor. `obj.p = v` must invoke the
# @p.setter (with its side effects), and a getter must NOT be clobbered when a
# setter is also defined (both are `def p`, stored under distinct symbols).
# Previously the setter overwrote the getter symbol and `obj.p = v` was a
# shadowing field store -- the setter's side effects were lost
# (a32_property_setter_side_effect / b6_property_covariant_override).


class C:
    def __init__(self) -> None:
        self._v: int = 0
        self.flag: int = 0

    @property
    def v(self) -> int:
        return self._v

    @v.setter
    def v(self, x: int) -> None:
        self._v = x
        self.flag = 1


def main() -> None:
    c = C()
    assert c.v == 0       # getter works even though a setter exists
    c.v = 7               # invokes the setter
    assert c._v == 7      # setter wrote the backing field
    assert c.flag == 1    # setter side effect observed
    assert c.v == 7       # getter reflects the new value


main()
