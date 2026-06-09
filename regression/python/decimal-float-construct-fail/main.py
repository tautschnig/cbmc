# Soundness: Decimal(<float>) cannot be modelled exactly in 64 bits, so it
# is an unconstrained finite Decimal. It must NOT be silently 0 (which
# would let this equality false-prove).
from decimal import Decimal

x: Decimal = Decimal(1.1)
assert x == Decimal(0)
