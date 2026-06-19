# The undefined-annotation NameError check must NOT fire when annotations
# are valid, deferred, or string forward-references. All paths here must
# verify SUCCESSFUL.
from __future__ import annotations  # PEP 563: annotations are not evaluated
from typing import Optional


class MyClass:
    pass


# Under __future__ annotations, even an undefined bare name is deferred
# (stringified) and never raises.
def deferred() -> NotYetDefined:  # noqa: F821
    return 1


# Valid annotations: builtins, user classes, imported typing names.
def good(x: int, m: MyClass) -> Optional[int]:
    return x


# A string forward-reference is never evaluated.
def fwd() -> "AlsoUndefined":  # noqa: F821
    return 2


assert deferred() == 1
assert good(5, MyClass()) == 5
assert fwd() == 2
