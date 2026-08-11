def block_0():
    def f(*args):
        return sum(args)
    assert f(1, 2, 3) == 6

def block_3():
    def f(**kwargs):
        return kwargs.get("x", 0)
    assert f(x=42) == 42

block_0()
block_3()
