# PLR §6.7 — augmented assignment for // and % must use Python's
# floored-division semantics, not C's truncation.
#
# 7 // -2 = -4 (floor toward -inf), not -3 (truncation toward 0).
# 7 % -3 = -2 (sign matches divisor), not 1 (sign matches dividend).
#
# convert_bin_op already had this; convert_aug_assign was using bare
# div_exprt/mod_exprt, which are C-style. The augmented forms now
# mirror the bin-op runtime form (q -= 1 / r += b when sign(a) !=
# sign(b) and the remainder is non-zero).

def floordiv_neg() -> None:
    x: int = 7
    x //= -2
    assert x == -4

def mod_neg() -> None:
    x: int = 7
    x %= -3
    assert x == -2

floordiv_neg()
mod_neg()
