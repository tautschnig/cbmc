x = 1

class C:
    def f(self) -> int:
        return x

c = C()
assert c.f() == 1
