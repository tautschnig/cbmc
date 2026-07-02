# PLR §7.2: chained assignment binds ONE object to all targets, so `a` and `b`
# are the SAME instance -- `a.v = 99` is visible as `b.v == 99`. The assertion
# `b.v == 1` is therefore false. CPython: AssertionError; VERIFICATION FAILED.
class C:
    def __init__(self):
        self.v = 1


a = b = C()
a.v = 99
assert b.v == 1
