# Per-instance provenance (plan §1): iterating an Any-typed value holding a
# CLASS instance dispatches the single-owner __iter__ -- the stub-response
# shape (boto3 corpus: ecs/ses). `iter([])` is the CPython-faithful form
# (returning a plain list would raise "TypeError: iter() returned
# non-iterator" -- pinned by iter-protocol-non-iterator); the model erases
# iter(x) to x, so the dispatch sees an empty list and the loop body never
# runs. Before the dispatch the loop iterated a wholly-nondet list view and
# the element subscript false-alarmed. The dispatch guard is
# IDENTITY-refined (__class_tag), not just tag == CLASS.
from typing import Any


class Resp:
    def __getitem__(self, k):
        return Resp()

    def __iter__(self):
        return iter([])


def get() -> Any:
    return Resp()


r: Any = get()
for c in r["items"]:
    x = c["name"]
assert True
