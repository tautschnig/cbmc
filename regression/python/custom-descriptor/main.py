# differential2 §11b: custom (non-@property) data descriptor. A class
# attribute bound to an instance of a class defining __get__ dispatches
# through __get__ on access. The class attribute is now typed as the
# descriptor class and the read emits Doubler.__get__(descriptor, c,
# None) (resolved via the MRO), instead of returning the stored value.
class Doubler:
    def __get__(self, obj, objtype=None) -> int:
        return 21 * 2


class C:
    d = Doubler()


c = C()
assert c.d == 42
