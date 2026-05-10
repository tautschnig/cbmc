# Method call on a tagged-union CLASS value — routed via
# __class_ptr to the first class with the matching method.


class Container:
    def __init__(self, v: int) -> None:
        self.v = v

    def value(self) -> int:
        return self.v


def mk(flag: bool):
    if flag:
        return Container(7)
    return Container(11)


c = mk(True)
# c is tagged-union; .value() resolves through __class_ptr
v = c.value()
assert v == 7 or v == 11
