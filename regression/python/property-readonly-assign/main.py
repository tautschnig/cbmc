# PLR §3.3.2: assigning to a read-only @property (a getter with no @x.setter)
# raises AttributeError ("property '...' object has no setter"). emit_property_set
# now raises AttributeError when the attr is a property with no setter across the
# MRO (instead of a shadowing store). CPython: AttributeError; expected: FAILED.
class C:
    @property
    def x(self) -> int:
        return 1


c = C()
c.x = 5
