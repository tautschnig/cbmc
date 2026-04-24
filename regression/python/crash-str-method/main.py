class Foo:
    def __init__(self, name: str) -> None:
        self.name = name

    def get_name(self) -> str:
        return self.name

f = Foo("test")
assert len(f.get_name()) == 4
