# Structure-of-arrays List[TypedDict] (spike): direct stub return,
# closed-form map over the row-index binding, element-wise
# relations at any length, len() through the SoA length member.
from typing import TypedDict, List


class App(TypedDict):
    appId: int


def list_apps() -> List[App]: ...


xs = list_apps()
assert len(xs) >= 0
ids = [a['appId'] for a in xs]
plus = [a['appId'] + 1 for a in xs]
assert len(ids) == len(xs)
if len(xs) > 2:
    assert plus[1] == ids[1] + 1
