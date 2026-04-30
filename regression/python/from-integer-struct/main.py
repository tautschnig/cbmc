# Test that from_integer on struct types doesn't crash
class MyClass:
    value: int = 0

obj: MyClass = MyClass()
assert obj.value == 0
