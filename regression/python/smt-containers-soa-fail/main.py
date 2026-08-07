# twins: (1) element INDEPENDENCE -- the SoA rows must not falsely
# alias (ids[0] == ids[1] unprovable); (2) the ROW-ESCAPE gate -- a
# row used as a VALUE must not pun to its index (rows[0] == 0 was a
# demonstrated false proof before the occurs-check); (3) stale-copy
# soundness -- a length fact must not survive an in-place append.
from typing import TypedDict, List


class App(TypedDict):
    appId: int


def list_apps() -> List[App]: ...


xs = list_apps()
ids = [a['appId'] for a in xs]
if len(xs) > 1:
    assert ids[0] == ids[1]
