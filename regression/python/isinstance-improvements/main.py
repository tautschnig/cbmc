# isinstance improvements — per-class CLASS dispatch:
#
#   - Tuple-of-types, single-name, and inheritance all use
#     __class_tag comparison through __class_ptr, so
#     isinstance(x, Foo) returns True only when x really is
#     a Foo (or a Foo subclass).


class Foo:
    def __init__(self) -> None:
        self.tag = 1


class Bar:
    def __init__(self) -> None:
        self.tag = 2


class FooChild(Foo):
    def __init__(self) -> None:
        super().__init__()
        self.extra = 10


def choose(flag: bool):
    if flag:
        return Foo()
    return Bar()


# Tuple-of-types over a tagged-union return value.
x = choose(True)
assert isinstance(x, (Foo, Bar))

# Single-name isinstance over a tagged-union (union tag).
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


# Precise dispatch: FooChild satisfies isinstance Foo (subclass).
def pick_class(i: int):
    if i == 0:
        return Foo()
    return FooChild()


q = pick_class(1)
assert isinstance(q, Foo)  # true for both Foo and FooChild
