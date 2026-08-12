from typing import TypedDict, List


class Reading(TypedDict):
    ts: int


def get_readings() -> List[Reading]: ...


rs = get_readings()
# The sortedness contract as CODE (the original's docstring is not
# semantics): all-pairs form.
assume(all(rs[i]['ts'] <= rs[j]['ts']
           for i in range(len(rs)) for j in range(i + 1, len(rs))))
recent = [r['ts'] for r in rs if r['ts'] > 1000]
for i in range(1, len(recent)):
    assert recent[i - 1] <= recent[i]
