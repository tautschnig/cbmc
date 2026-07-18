# Keyword args through the pv-receiver dispatch bind into the trailing
# **kwargs dict (previously DROPPED: the call went out with too few
# arguments and the GOTO layer nondet-filled the kwargs param -- losing
# required-kwarg contract precision and exposing a latent symex crash).
# A no-keyword call binds the EMPTY dict (CPython), so the stub's
# `"Name" in kwargs` contract is decidable both ways.
from typing import Any


class Svc:
    def op(self, **kwargs: Any) -> int:
        if "Name" in kwargs:
            return 1
        return 0


def get() -> Any:
    return Svc()


c: Any = get()
r1: Any = c.op(Name="x")
r2: Any = c.op()
assert r1 == 1
assert r2 == 0
