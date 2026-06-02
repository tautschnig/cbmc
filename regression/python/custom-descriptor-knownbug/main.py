# differential2 §11b: custom (non-@property) data descriptors. A class
# attribute that is an instance of a class defining __get__/__set__
# should dispatch through __get__ on access. CBMC does not type a
# class-level descriptor instance as its descriptor class nor dispatch
# __get__, so c.d is not the getter's result. Modeling this needs the
# instance-dict / descriptor protocol as first-class storage.
class Doubler:
    def __get__(self, obj, objtype=None) -> int:
        return 21 * 2


class C:
    d = Doubler()


c = C()
assert c.d == 42
