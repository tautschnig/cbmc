# PLR §3.1: class instances are passed by reference. A method that
# mutates a class-typed parameter must affect the caller's object, just
# like a free function does. (TypedDict parameters are excluded — they
# are passed as dict literals, not by reference.)
class Box:
    v: int

    def __init__(self, x: int = 0):
        self.v = x


class Mutator:
    def bump(self, b: Box):
        b.v = b.v + 1


m = Mutator()
box = Box(10)
m.bump(box)
assert box.v == 11
