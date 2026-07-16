# Per-instance provenance phase 1 (plan §1): iterating an Any-typed value
# holding a CLASS instance dispatches the single-owner __iter__ -- the
# stub-response shape (boto3 corpus: ecs/ses). The stub returns [] so the
# loop body never runs; before the dispatch the loop iterated a wholly-
# nondet list view and the element subscript false-alarmed. The dispatch
# guard is IDENTITY-refined (__class_tag), not just tag == CLASS.
from typing import Any


class Resp:
    def __getitem__(self, k):
        return Resp()

    def __iter__(self) -> list:
        return []


def get() -> Any:
    return Resp()


r: Any = get()
for c in r["items"]:
    x = c["name"]
assert True
