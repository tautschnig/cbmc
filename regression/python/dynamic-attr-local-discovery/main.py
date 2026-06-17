# PLR §6.3.1: instance attributes can be created from outside the class, on a
# LOCAL variable bound to an instance (not only a typed parameter). A
# whole-program pre-pass discovers `<local>.attr = ...` and declares the attr
# as a struct field, so the assigned value reads back precisely.


class C:
    def __init__(self):
        self.x = 1


# Module-level local bound to C().
c = C()
c.y = 42
assert c.y == 42
assert c.x == 1  # declared attr unaffected


# Function-local bound to C().
def f():
    d = C()
    d.z = 7
    assert d.z == 7


f()


# Annotation-bound local.
e: C = C()
e.w = 5
assert e.w == 5
