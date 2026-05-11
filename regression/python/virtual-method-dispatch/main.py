# PLR §3.3.2 virtual method dispatch through __class_tag.
# Two classes share a method name; the tagged-union value
# dispatches to the right one based on its runtime class
# tag.


class Dog:
    def __init__(self) -> None:
        pass

    def sound(self) -> int:
        return 1


class Cat:
    def __init__(self) -> None:
        pass

    def sound(self) -> int:
        return 2


def pick(flag: bool):
    if flag:
        return Dog()
    return Cat()


# tagged-union value — sound() must dispatch via __class_tag.
d = pick(True)
c = pick(False)

# Dog's sound returns 1; Cat's returns 2.
# Without virtual dispatch these both return 1 (the first
# matching class wins). With virtual dispatch they differ.
assert d.sound() == 1
assert c.sound() == 2
