class Wrapper:
    def __init__(self, name: str) -> None:
        self.name = name

    def get_name(self) -> str:
        return self.name

class Container:
    def __init__(self) -> None:
        self.item = Wrapper("test")

c = Container()
assert len(c.item.name) == 4
