# Regression: lambda x: (lambda y: x + y) — a lambda that returns a
# lambda — must be lowered to a closure-bound call rather than an
# assignment of a code-typed value to a variable. Previously the
# frontend emitted `inner := <lambda symbol>` which CBMC's symex
# rejects with "assignment to 'symbol' not handled".

f = lambda x: (lambda y: x + y)
g = f(5)
assert g(10) == 15
assert g(0) == 5
assert g(-3) == 2

# Inside a function body too.
def test() -> None:
    outer = lambda a: (lambda b: a * b)
    times_three = outer(3)
    assert times_three(4) == 12

test()
