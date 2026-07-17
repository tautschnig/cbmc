# Class instance into a LIST-typed slot converts through ITS OWN __iter__
# (PLR §3.3.1): `return resp.get(k, [])` under a `-> List[...]` return
# annotation flows a response OBJECT into a list slot; safe_typecast could
# only relabel/nondet it, so the returned list had nondet length and every
# downstream use false-alarmed (bedrock's list_foundation_models shape).
# The stub's iter([]) yields nothing, so the result is length 0.
from typing import Any, Dict, List


class Resp:
    def get(self, k, default=None):
        return Resp()

    def __iter__(self):
        return iter([])


def fetch() -> Any:
    return Resp()


def lst() -> List[Dict[str, Any]]:
    r: Any = fetch()
    return r.get("items", [])


ms = lst()
assert len(ms) == 0
