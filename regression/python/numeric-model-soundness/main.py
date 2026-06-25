# Soundness (P2 audit lock-in): the bytes / complex / Decimal models must not
# false-prove. Each assertion is genuinely FALSE, so verification must FAIL.
from decimal import Decimal
assert b"abc"[0] == 99               # bytes index (real 97)
assert len(b"abc") == 5              # bytes len (real 3)
assert ((1 + 2j) + (3 + 4j)) == (1 + 1j)   # complex add (real 4+6j)
assert abs(3 + 4j) == 0              # complex abs (real 5)
assert (Decimal("0.1") + Decimal("0.2")) == Decimal("0.4")   # exact base-10 (real 0.3)
