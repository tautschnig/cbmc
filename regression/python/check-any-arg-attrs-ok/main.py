# --python-check-any-arg-attrs must NOT fire when the accessed attribute exists
# on the concrete argument's class.
class C:
    def real(self) -> int:
        return 1

def use(p):
    return p.real()      # real() IS on C -> no attribute-error

use(C())
