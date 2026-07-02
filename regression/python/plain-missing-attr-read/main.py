# PLR §3.3.2: reading an attribute that exists nowhere on a class with a CLOSED
# attribute set (no __getattr__/__getattribute__, no metaclass/decorator, known
# bases, and the program uses no setattr/__dict__/vars) raises AttributeError.
# `b` is assigned nowhere, so `c.b` cannot exist. CPython: AttributeError.
class C:
    def __init__(self):
        self.a = 1


c = C()
x = c.b
