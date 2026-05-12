# Inter-procedural dict-literal propagation. A function
# returning a dict-literal with constant keys should
# convey that key-set to the caller so subscript lookups
# on the assigned variable don't spuriously trip KeyError.

from typing import Dict


def get_policies() -> Dict[str, int]:
    return {"managed": 1, "inline": 2}


d = get_policies()
# Both keys must be known to exist; no KeyError on lookup.
m = d["managed"]
i = d["inline"]
assert True  # reaching here means no KeyError raised.
