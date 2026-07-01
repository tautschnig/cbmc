# KNOWNBUG: assigning to a read-only @property (a getter with no @x.setter) raises
# AttributeError ('can't set attribute'). The frontend accepts the assignment
# (shadowing store). Desired: VERIFICATION FAILED.
class C:
    @property
    def x(self) -> int:
        return 1


c = C()
c.x = 5
