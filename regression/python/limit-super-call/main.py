# Python Language Reference §6.3.4: super()
# super().__init__() should call parent's __init__
class Base:
    def __init__(self) -> None:
        self.x: int = 10

class Derived(Base):
    def __init__(self) -> None:
        super().__init__()
        self.y: int = 20

d = Derived()
assert d.x == 10
assert d.y == 20
