# PLR §3.2: complex class attribute patterns
# Class attribute shadowing by instance attributes
class MyClass:
    class_attr: int = 1
    def __init__(self, value: int) -> None:
        self.data: int = value

obj = MyClass(10)
obj.class_attr = 2
assert obj.class_attr == 2
assert MyClass.class_attr == 1
