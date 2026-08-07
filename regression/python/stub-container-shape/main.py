# Stub-returned containers are WELL-FORMED values of the annotated
# shape (perf-study findings t4_neglen + d1_bindname):
# len >= 0 always; TypedDict container fields are REAL boxed
# containers with well-formed lengths, not arbitrary nondet values.
from typing import List, TypedDict


def get_names() -> List[str]: ...


ns = get_names()
assert len(ns) >= 0          # Python tautology; FAILED before (length=-1)
