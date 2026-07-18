# Constant-directed dispatcher folding (perf whole-group): a literal-keyed
# call to a pure dispatcher (`if p == "lit": return C()` chain + trailing
# `assert False` default) folds to the selected branch's construction at
# conversion time -- the N-way branch-join never reaches symex (the boto3
# client() chain: 31 branches made mediaconvert_manager OOM at 8.5 GB; the
# fold brought it to 0.9 GB). Semantics preserved: the fold is invisible
# (boxed Any result, no-arg ctor with DEFAULTS, identity stamped); an
# unmatched literal folds to the dispatcher's own assert-False contract.
from typing import Any


class A:
    def kind(self) -> int:
        return 1


class B:
    def kind(self) -> int:
        return 2


def make(name: str) -> Any:
    if name == "a":
        return A()
    if name == "b":
        return B()
    assert False, "unknown"
    return A()


x: Any = make("a")
y: Any = make("b")
assert x.kind() == 1
assert y.kind() == 2
