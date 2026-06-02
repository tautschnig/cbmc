# differential2 §11 inheritance: missing super().__init__() leaves an
# inherited declared field unassigned, so reading it is an
# AttributeError. Base declares and assigns x in its __init__; Sub
# overrides __init__ but does NOT call super().__init__(), so a Sub
# instance never assigns x.
class Base:
    x: int

    def __init__(self, v: int) -> None:
        self.x = v


class Sub(Base):
    def __init__(self, v: int) -> None:
        self.y = v


s = Sub(5)
v = s.x
