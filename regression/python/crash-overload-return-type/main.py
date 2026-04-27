# PLR §8.7: @overload with different return types
# Functions returning different class types on different paths
# crash in symex: "assignments must be type consistent"
class Foo:
    def __init__(self) -> None:
        pass

class Bar:
    def __init__(self) -> None:
        pass

def create(i: int):
    if i == 0:
        return Foo()
    return Bar()

f = create(0)
