class MyClass:
    def get_value(self) -> int:
        return 42

obj: MyClass = MyClass()
method = obj.get_value
result: int = method()
assert result == 42
