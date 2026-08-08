# SoA row views: the chained field read xs[i]['f'] resolves to
# f_data[i] (with the PLR 6.10.2 IndexError obligation), consistent
# with the closed-form map at the same index.
from typing import TypedDict, List


class App(TypedDict):
    appId: int


def list_apps() -> List[App]: ...


xs = list_apps()
if len(xs) > 3:
    v = xs[2]['appId']
    ids = [a['appId'] for a in xs]
    assert v == ids[2]
