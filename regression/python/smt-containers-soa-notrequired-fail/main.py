# twin (perf-study t5, SoA edition): an UNGUARDED read of a
# NotRequired field carries its KeyError obligation -- the row may
# omit the key.
from typing import TypedDict, List, NotRequired


class App(TypedDict):
    appId: int
    note: NotRequired[int]


def list_apps() -> List[App]: ...


xs = list_apps()
if len(xs) > 1:
    v = xs[0]['note']
    assert v == v
