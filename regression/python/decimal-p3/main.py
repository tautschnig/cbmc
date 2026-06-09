# Decimal P3: exact terminating division, ROUND_HALF_EVEN quantize,
# special values, and value-based != (which must negate __eq__).
from decimal import Decimal

# Exact terminating division.
q: Decimal = Decimal(1) / Decimal(4)
assert q._int == 25
assert q._exp == -2
assert Decimal(6) / Decimal(3) == Decimal(2)

# quantize, ROUND_HALF_EVEN.
a: Decimal = Decimal("3.14159").quantize(Decimal("0.01"))
assert a._int == 314
assert a._exp == -2
b: Decimal = Decimal("2.675").quantize(Decimal("0.01"))
assert b._int == 268

# Special values.
n: Decimal = Decimal("NaN")
assert n.is_nan()
assert not (n == n)
assert n != n
i: Decimal = Decimal("Infinity")
assert i.is_infinite()

# Value-based equality / inequality (different representation, same value).
assert Decimal("1.0") == Decimal("1.00")
assert not (Decimal("1.0") != Decimal("1.00"))
assert Decimal("1.5") != Decimal("2.5")
