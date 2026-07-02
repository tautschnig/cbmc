# PLR §3.3.2.4: __slots__ gives a class a CLOSED attribute set (no __dict__), so
# reading an attribute that is not a slot / method / class-attr raises
# AttributeError. `c.b` below is not in __slots__ -> AttributeError.
# CPython: AttributeError; expected: VERIFICATION FAILED.
class C:
    __slots__ = ("a",)


c = C()
c.a = 1
x = c.b
