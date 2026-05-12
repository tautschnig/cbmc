# PLR §3.3.1 data model — custom __contains__ dunder
# method. 'x in obj' dispatches to obj.__contains__(x)
# when the class defines one.

class Box:
    def __init__(self) -> None:
        pass

    def __contains__(self, x: int) -> bool:
        return x == 2


b = Box()
assert 2 in b
assert not (3 in b)
assert 4 not in b
