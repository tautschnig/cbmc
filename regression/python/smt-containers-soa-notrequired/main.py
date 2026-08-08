# PEP 589 requiredness in SoA lists: OPTIONAL fields (NotRequired)
# get per-row PRESENCE arrays; required-field reads stay safe and
# required-only maps keep the closed form.
from typing import TypedDict, List, NotRequired


class App(TypedDict):
    appId: int
    note: NotRequired[int]


def list_apps() -> List[App]: ...


xs = list_apps()
ids = [a['appId'] for a in xs]
assert len(ids) == len(xs)
if len(xs) > 1:
    v = xs[0]['appId']
    assert v == v
