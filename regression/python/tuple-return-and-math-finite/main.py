# PLR §6.10.5: 'return a, b' is an implicit-tuple return.
# The return-type inference scanner only handled scalar
# returns and constructor returns; Tuple returns fell through
# and the function's inferred return type stayed empty,
# defaulting to int. Calling the function then produced a
# safe_typecast from tuple struct → int, returning 0.
#
# Fix: when the scanner sees a Tuple return AND no return
# type has been inferred yet, build a python_tuple_type with
# element types inferred from the operands.

def swap(a: int, b: int):
    return b, a


def tuple_return_unpacking() -> None:
    a = 2
    b = 4
    a, b = swap(a, b)
    assert a == 4
    assert b == 2


def tuple_return_capture() -> None:
    result = swap(2, 4)
    assert result == (4, 2)


tuple_return_unpacking()
tuple_return_capture()
