# isinstance improvements:
#
#   1. tuple-of-types form: isinstance(x, (A, B)) where x is a
#      tagged-union value now ORs the per-tag checks instead of
#      failing through to the generic else-branch.
#   2. user-class on tagged-union: isinstance(x, MyClass) where x
#      is a python_value_type returns whether the tag is CLASS
#      (coarse but sound).

from typing import Union


class Foo:
    def __init__(self) -> None:
        self.tag = 1


class Bar:
    def __init__(self) -> None:
        self.tag = 2


def choose(flag: bool):
    if flag:
        return Foo()
    return Bar()


# Tuple-of-types over a tagged-union return value.
x = choose(True)
assert isinstance(x, (Foo, Bar))

# Single-name isinstance over a tagged-union (CLASS tag).
y = choose(False)
assert isinstance(y, Foo) or isinstance(y, Bar)


# Tuple-of-types over primitive.
def pick(i: int):
    if i == 0:
        return 5
    if i == 1:
        return 5.5
    return "hi"


p = pick(0)
assert isinstance(p, (int, float, str))
