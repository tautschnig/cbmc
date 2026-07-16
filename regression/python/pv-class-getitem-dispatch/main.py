# CORE (PLR §3.3.1): a class instance boxed in a python_value (Any) dispatches
# __getitem__ on subscript -- a chained `r["A"]["B"]` where each access returns
# a fresh instance must not raise a spurious not-subscriptable TypeError (the
# boto3 stub response-object shape; 5 real-world benchmarks). Single-owner
# dispatch: exactly one user class defining __getitem__ routes the call.
from typing import Any


class AnyD:
    def __getitem__(self, key):
        return AnyD()


def f() -> Any:
    return AnyD()


r: Any = f()
x = r["A"]["B"]
assert True
