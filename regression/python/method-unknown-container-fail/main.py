# Each assertion fails in CPython. A method of a list or dict that the model
# lacks returned a value and left the receiver unchanged, so the assertions
# on the old contents verified; an unknown method of a list display raised
# nothing, so the assertion after it verified.
def setitem():
    x = [1, 2]
    x.__setitem__(0, 5)
    assert x[0] == 1, "list"


def dict_setitem():
    d = {"a": 1}
    d.__setitem__("a", 5)
    assert d["a"] == 1, "dict"


def iadd():
    x = [1]
    x.__iadd__([2])
    assert len(x) == 1, "iadd"


def unknown():
    try:
        [1].foo()
    except TypeError:
        pass
    assert False, "unknown"


setitem()
dict_setitem()
iadd()
unknown()
