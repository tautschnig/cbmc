# PLR §8.7: function with untyped parameter
def foo(s):
    x: int = len(s)
    assert x == 4

foo("test")
