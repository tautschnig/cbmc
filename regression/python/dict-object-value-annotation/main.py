# CORE: a `-> Dict[str, object]` (non-"safe" value type) return annotation used
# to lower to python_int_type() -- the callee's dict result was INT-typed, so a
# caller's subscript raised a spurious "not subscriptable" TypeError (the
# dominant real-world FP cluster: 14 of the 51 boto3 benchmarks). It now lowers
# to dict[str, python_value] (Any values), keeping the DICT shape: the subscript
# is precise and the value reads back correctly.
import typing
from typing import Any, Dict


def f(u: str) -> typing.Dict[str, object]:
    d: dict[str, Any] = {"valid": False, "n": 5}
    d["valid"] = True
    return d


r: Any = f("x")
if r["valid"]:
    x = 1
assert r["n"] == 5
