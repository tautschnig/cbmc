# Check-only for-loop over an SoA list: the representative lift
# composes with the row-index binding -- the loop variable IS the
# index and a['f'] resolves to f_data[a], so the forall obligation
# proves at any length with the body run once.
from typing import TypedDict, List


class App(TypedDict):
    appId: int


def list_apps() -> List[App]: ...


xs = list_apps()
for a in xs:
    assert a['appId'] == a['appId']
