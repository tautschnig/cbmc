from typing import TypedDict, List


class Reading(TypedDict):
    ts: int


def get_readings() -> List[Reading]: ...


rs = get_readings()
# The sortedness contract as CODE (the original's docstring is not
# semantics): all-pairs form.
recent = [r['ts'] for r in rs if r['ts'] > 1000]
for i in range(1, len(recent)):
    assert recent[i - 1] <= recent[i]
