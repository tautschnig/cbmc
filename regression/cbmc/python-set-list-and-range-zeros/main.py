# Two related buffer/representation fixes:
#
# 1. PLR §6.10.2: set(list_of_ints) must produce a bitmap-set
#    (not a list-shaped struct) so that the binary operators
#    |, &, -, ^ — which work on python_set_type bitmaps —
#    interoperate with constructed sets the same way they do
#    with set literals {1, 2, 3}. Without this, sorted(set([..])
#    | set([..])) doesn't return the expected list because the
#    operands have different shapes.
#
# 2. PLR §6.10.1: range() materialises its values into a list,
#    but the data buffer beyond the populated length was
#    uninitialised. Struct-equality with a list literal (whose
#    trailing slots are zeros) failed because the literal had
#    zeros in those positions. Fix: zero-init the data buffer
#    at the start, then the conditional-fill leaves trailing
#    zeros intact.

def set_from_list_union() -> None:
    xs = set([1, 2, 3])
    ys = set([3, 4, 5])
    assert sorted(xs | ys) == [1, 2, 3, 4, 5]


def set_from_list_intersection() -> None:
    xs = set([1, 2, 3, 4])
    ys = set([3, 4, 5])
    assert sorted(xs & ys) == [3, 4]


def list_of_range() -> None:
    assert list(range(3)) == [0, 1, 2]


def list_of_range_with_start() -> None:
    assert list(range(2, 5)) == [2, 3, 4]


set_from_list_union()
set_from_list_intersection()
list_of_range()
list_of_range_with_start()
