# Class INT fields under --python-unbounded-ints are int-id handles
# (inttab(h) denotation) -- the int twin of the native string-field
# handles: an inline integer_typet member made the class struct
# variable-width and aborted unpack_struct on byte-imaged identity reads
# (the boto3 `exceptions` class attribute under mediaconvert /
# aws_untagged). Reads via the convert_attribute choke point; writes via
# coerce_assign_rhs (int_to_handle). Full precision preserved (a value
# beyond 2**64 round-trips exactly). Also pins: pv-int comparisons
# against literals typed by the DENOTATION (truthiness / None-sentinel
# arms mixed integer with i64 constants -- smt2 conversion aborts), and
# non-constant Int->BV narrowing (int2bv; e.g. negative-index stores).
from typing import Any


class C:
    n: int

    def __init__(self) -> None:
        self.n = 3


def f() -> Any:
    return C()


r: Any = f()
if isinstance(r, C):
    pass
c = C()
assert c.n == 3
c.n = 10 ** 25
assert c.n == 10 ** 25
assert c.n > 2 ** 64


def g(x: Any) -> bool:
    if x:
        return True
    return False


assert g(5)
assert not g(0)

ns = [10, 20, 30]
j = -1
ns[j] = 99
assert ns[2] == 99
