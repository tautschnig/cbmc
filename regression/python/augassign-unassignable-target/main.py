# Store-target assignability guard: a CHAINED pv subscript aug-assign
# (`d['a']['b'] += 1` on an Any dict) lowers its target to a sound-nondet
# rvalue; emitting that as an ASSIGN lhs aborted symex
# (l2_rename_rvalues "case nondet_symbol not handled" -- hit on
# aws_untagged's impact_analysis['resource_breakdown'][t] += 1). The
# sound fallback HAVOCS the root container (a later read is nondet, so
# the wrong-value assert below must FAIL, not prove).
from typing import Any


def f() -> Any:
    return {"a": {"b": 0}}


d: Any = f()
d["a"]["b"] += 1
assert d["a"]["b"] == 99
