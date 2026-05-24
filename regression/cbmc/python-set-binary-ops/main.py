# PLR §6.10.2: set binary operators (|, &, -, ^) on int-bitmap
# sets. The bitmap operations themselves were already implemented
# (bitor / bitand / bitand+bitnot / bitxor); the missing piece was
# materialising the resulting bitmap into a list when the caller
# does sorted(set) or similar list-conversion. Without this,
# sorted() returned nondet, breaking equality checks against
# list literals.
#
# The fix: in convert_call's sorted() handler, when the argument
# is python_set_type, iterate bits 0..63 of the bitmap (in
# increasing order, so the resulting list is naturally sorted)
# and append `offset + bit` to a fresh list for each set bit.
# Zero-init the data buffer so trailing slots match the literal.

def union() -> None:
    xs = {1, 2, 3}
    ys = {3, 4, 5}
    assert sorted(xs | ys) == [1, 2, 3, 4, 5]


def intersection() -> None:
    xs = {1, 2, 3, 4}
    ys = {3, 4, 5}
    assert sorted(xs & ys) == [3, 4]


def difference() -> None:
    xs = {1, 2, 3, 4}
    ys = {3, 4}
    assert sorted(xs - ys) == [1, 2]


def symmetric_diff() -> None:
    xs = {1, 2, 3}
    ys = {3, 4, 5}
    assert sorted(xs ^ ys) == [1, 2, 4, 5]


union()
intersection()
difference()
symmetric_diff()
