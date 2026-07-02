# No-false-positive guard: a class is NOT flagged when the attribute could exist.
# A field set in any method, a class-level attr, a method, an inherited attr, and
# an attribute assigned via `X.attr =` anywhere (even on an alias) must all read
# without AttributeError; a __getattr__ class and a dunder are never flagged.
class B:
    def __init__(self):
        self.a = 1


class C(B):
    z = 9

    def cfg(self):
        self.b = 2

    def m(self):
        return 1


c = C()
c.cfg()
assert c.a == 1     # inherited instance attr
assert c.z == 9     # class-level attr
assert c.b == 2     # set in a (non-init) method
assert c.m() == 1   # method
d = c
d.viaAlias = 7      # X.attr = via an alias -> name known program-wide
_ = c.viaAlias
_ = c.__class__     # object dunder


class G:
    def __getattr__(self, n):
        return 42


g = G()
_ = g.anything      # __getattr__ class: never flagged
