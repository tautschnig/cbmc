# twin + vacuity: a property false for SOME element must fail.
from typing import TypedDict, List


class App(TypedDict):
    appId: int


def list_apps() -> List[App]: ...


xs = list_apps()
for a in xs:
    assert a['appId'] == 7
