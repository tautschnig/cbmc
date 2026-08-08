# twins: a BARE row read (no value representation -- the index-pun
# false-proof class) rejects loudly; an out-of-range row read keeps
# its IndexError obligation.
from typing import TypedDict, List


class App(TypedDict):
    appId: int


def list_apps() -> List[App]: ...


xs = list_apps()
v = xs[0]['appId']       # length unconstrained: IndexError reachable
assert v == v
