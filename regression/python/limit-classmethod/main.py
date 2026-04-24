# Python Language Reference §8.7: class methods
# @classmethod should be callable on the class itself
class MyClass:
    @classmethod
    def create(cls) -> int:
        return 42

x: int = MyClass.create()
assert x == 42
