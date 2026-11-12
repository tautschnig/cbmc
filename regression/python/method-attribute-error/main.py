# A method that a str, set, float or complex does not have raises
# AttributeError; it was nondet. A subclass keeps its methods.
class S(str):
    def f(self) -> int:
        return 1

assert S("a").f() == 1
x = {1, 2}
try:
    x.foo()
    assert False
except AttributeError:
    pass
try:
    "a".foo()
    assert False
except AttributeError:
    pass
try:
    (1.5).foo()
    assert False
except AttributeError:
    pass
try:
    y = "a".foo
    assert False
except AttributeError:
    pass
try:
    z = {1}.foo
    assert False
except AttributeError:
    pass
