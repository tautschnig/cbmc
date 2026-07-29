# PLR §4.2.1: "If a name is bound in a block, it is a local variable of
# that block." A nested function assigning a name that ALSO exists in
# the enclosing scope binds a fresh local; it must not capture the
# enclosing variable. The capture scan previously value-captured such
# names, producing an ill-typed parameter when the two scopes bind
# different types (a graceful conversion abort on agent-generated AWS
# code where both scopes bound `result`).
def outer():
    v = 5

    def inner():
        v = 7
        return v

    r = inner()
    assert r == 7
    assert v == 5


def type_mismatch():
    def inner(b):
        result = {'k': b}
        return result.get('k', 0)

    x = inner(3)
    # Same NAME, different TYPE in the enclosing scope: previously the
    # dict-typed inner `result` was seeded from this int-typed symbol.
    result = 1
    assert x == 3
    assert result == 1


outer()
type_mismatch()
