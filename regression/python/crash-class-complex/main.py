class MyClass:
    class_attr: int = 1

    def __init__(self, value: int) -> None:
        self.data = value

obj = MyClass(42)
assert obj.data == 42
