"""
Verification model of the `operator` module.

Functional counterparts of the Python operators — ``add(a, b)``
is ``a + b``, ``itemgetter(i)`` returns ``lambda x: x[i]``, etc.
All functions are implemented directly in Python to compose
cleanly with verification.
"""


# Arithmetic
def add(a, b):
    return a + b


def sub(a, b):
    return a - b


def mul(a, b):
    return a * b


def truediv(a, b):
    return a / b


def floordiv(a, b):
    return a // b


def mod(a, b):
    return a % b


def pow(a, b):
    return a ** b


def neg(a):
    return -a


def pos(a):
    return +a


def abs(a):
    return a if a >= 0 else -a


# Bitwise
def and_(a, b):
    return a & b


def or_(a, b):
    return a | b


def xor(a, b):
    return a ^ b


def inv(a):
    return ~a


def invert(a):
    return ~a


def lshift(a, b):
    return a << b


def rshift(a, b):
    return a >> b


# Comparison
def lt(a, b):
    return a < b


def le(a, b):
    return a <= b


def eq(a, b):
    return a == b


def ne(a, b):
    return a != b


def ge(a, b):
    return a >= b


def gt(a, b):
    return a > b


# Logical
def not_(a):
    return not a


def truth(a):
    return bool(a)


def is_(a, b):
    return a is b


def is_not(a, b):
    return a is not b


# Sequence
def concat(a, b):
    return a + b


def contains(a, b):
    return b in a


def countOf(a, b):
    count = 0
    for item in a:
        if item == b:
            count = count + 1
    return count


def indexOf(a, b):
    for i, item in enumerate(a):
        if item == b:
            return i
    raise ValueError("not in sequence")


def getitem(a, b):
    return a[b]


def setitem(a, b, c):
    a[b] = c
    return None


def delitem(a, b):
    del a[b]
    return None


def length_hint(obj, default=0):
    try:
        return len(obj)
    except TypeError:
        return default


# Callable constructors
class attrgetter:
    def __init__(self, *attrs):
        self._attrs = attrs

    def __call__(self, obj):
        if len(self._attrs) == 1:
            return getattr(obj, self._attrs[0])
        return tuple(getattr(obj, a) for a in self._attrs)


class itemgetter:
    def __init__(self, *items):
        self._items = items

    def __call__(self, obj):
        if len(self._items) == 1:
            return obj[self._items[0]]
        return tuple(obj[i] for i in self._items)


class methodcaller:
    def __init__(self, name, *args, **kwargs):
        self._name = name
        self._args = args
        self._kwargs = kwargs

    def __call__(self, obj):
        return getattr(obj, self._name)(*self._args, **self._kwargs)
