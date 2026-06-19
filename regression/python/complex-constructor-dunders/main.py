# PLR §6.10.1: complex() constructor — numeric dunder dispatch
# (__complex__ > __float__ > __index__), wrong-return-type TypeErrors,
# and the second-argument TypeError matrix.


class HasComplex:
    def __complex__(self) -> complex:
        return complex(1, 2)


class HasFloat:
    def __float__(self) -> float:
        return 3.5


class HasIndex:
    def __index__(self) -> int:
        return 4


class HasComplexAndFloat:
    def __complex__(self) -> complex:
        return complex(2, 3)

    def __float__(self) -> float:
        return 9.0


# Value dispatch.
assert complex(HasComplex()).real == 1.0
assert complex(HasComplex()).imag == 2.0
assert complex(HasFloat()).real == 3.5
assert complex(HasFloat()).imag == 0.0
assert complex(HasIndex()).real == 4.0
# __complex__ takes priority over __float__.
assert complex(HasComplexAndFloat()).real == 2.0
assert complex(HasComplexAndFloat()).imag == 3.0


# Wrong dunder return type -> TypeError.
class BadComplex:
    def __complex__(self) -> float:
        return 1.0


raised = False
try:
    complex(BadComplex())
except TypeError:
    raised = True
assert raised


# Second-argument TypeError matrix.
raised = False
try:
    complex("1", 2)  # string first arg + second arg
except TypeError:
    raised = True
assert raised

raised = False
try:
    complex(1, "2")  # string second arg
except TypeError:
    raised = True
assert raised

raised = False
try:
    complex(1, b"2")  # bytes second arg
except TypeError:
    raised = True
assert raised

# Valid forms still work.
assert complex(1, 2).real == 1.0
assert complex(3.0, 4.0).imag == 4.0
assert complex(real=1, imag=2).imag == 2.0
