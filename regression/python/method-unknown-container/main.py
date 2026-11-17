# A method that a list, tuple, dict or bytes made by a display does not have
# raises AttributeError (it returned an int); a constant int already did.
# bytes.count takes a start and an end (a TypeError was raised).
def attribute_error(kind: int) -> bool:
    try:
        if kind == 0:
            [1, 2].foo()
        elif kind == 1:
            x = (1, 2)
            x.foo()
        elif kind == 2:
            d = {"a": 1}
            d.foo()
        elif kind == 3:
            b"ab".foo()
        else:
            n = 5
            n.foo()
        return False
    except AttributeError:
        return True


assert attribute_error(0)
assert attribute_error(1)
assert attribute_error(2)
assert attribute_error(3)
assert attribute_error(4)
try:
    b"a b a".count(b"a", 1, 5)
    b"a b a".index(b"b", 0, 5)
    arity = True
except TypeError:
    arity = False
assert arity
