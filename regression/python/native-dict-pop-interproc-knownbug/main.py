# Native-only imprecision (recorded 2026-07-22, sound direction): dict.pop
# key-membership through a pv-typed key PARAMETER does not propagate to
# the caller's dict under --python-smt-strings (observed: the asserts
# below give spurious FAILED; never a false proof -- sound direction).
# The same pop DIRECT (no call) and the str-typed-parameter module-
# global shape are both precise; the loss is specific to the pv-typed
# key crossing the call boundary on the native backend. The
# default backend is precise on the same program. Desired outcome:
# VERIFICATION SUCCESSFUL.
from typing import Any


def drop(d: dict, key: Any) -> None:
    d.pop(key, None)


p: dict = {"a": 1, "b": 2}
drop(p, "b")
assert "b" not in p
assert "a" in p
