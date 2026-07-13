# CORE (PLR §8.7): a raising default in a METHOD is evaluated at class-body
# (def) time, so `class C: def m(self, a=[][0])` raises when C is defined --
# before m is ever called. CPython raises -> VERIFICATION FAILED.
class C:
    def m(self, a=[][0]):
        return 1


C()
