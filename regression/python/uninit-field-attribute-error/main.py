# differential2 §11 (uninitialized-field AttributeError). The field x
# is declared but assigned only on the flag=True path of __init__, so
# Obj(False).x is unbound and reading it raises AttributeError. The
# instance write sets a synthetic shadow flag; a read of a
# not-definitely-initialized bare-annotation field asserts that flag.
class Obj:
    x: int

    def __init__(self, flag: bool) -> None:
        if flag:
            self.x = 42


o = Obj(False)
v = o.x
