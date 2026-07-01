# PLR §3.3.2.4: __slots__ restricts a slots-enforced class's instance attributes;
# assigning an attribute not named in __slots__ (across the MRO) raises
# AttributeError. convert_class_def records __slots__ per class, and the
# attribute-store path emits AttributeError via slots_forbidden_attr when the
# class (and all its user-class bases) declare __slots__ and the attr is not a
# permitted slot. CPython: AttributeError; expected: VERIFICATION FAILED.
class C:
    __slots__ = ("a",)


c = C()
c.b = 5
