# PLR 8.7: a constructor/method with *args packs trailing positionals into a
# list (build_class_init_call now packs varargs, like the user-call path).
class C:
    def __init__(self, *parts):
        self.n = len(parts)
        self.first = parts[0] if len(parts) > 0 else -1


a = C(10, 20, 30)
assert a.n == 3
assert a.first == 10

b = C()
assert b.n == 0


class D:
    def __init__(self, name, *rest):
        self.name = name
        self.k = len(rest)


d = D("x", 1, 2)
assert d.name == "x"
assert d.k == 2
