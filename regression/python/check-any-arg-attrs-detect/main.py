# --python-check-any-arg-attrs: cross-function Any-erasure. `use`'s parameter is
# unannotated (Any); the caller passes a concrete C(); the body accesses
# p.missing() which C does not define -> attribute-error at the call site.
class C:
    def real(self) -> int:
        return 1

def use(p):
    return p.missing()   # C has no 'missing'

use(C())
