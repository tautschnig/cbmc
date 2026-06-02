# PLR §3.2: a method (instance or classmethod) with no return
# annotation whose body returns a class instance must be typed as that
# class, not default to int. Reading a field off the result then works.
class Box:
    val: int

    def __init__(self, v: int = 0):
        self.val = v

    @classmethod
    def create(cls):
        return Box(7)

    def clone(self):
        return Box(self.val)


a = Box.create()
assert a.val == 7

b = Box(3).clone()
assert b.val == 3
