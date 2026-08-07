# The ROW-ESCAPE gate: a row used as a VALUE must not pun to its
# index (rows[0] == 0 was a demonstrated false proof before the
# occurs-check rejected escaping bodies).
from typing import TypedDict, List


class App(TypedDict):
    appId: int


def list_apps() -> List[App]: ...


xs = list_apps()
rows = [a for a in xs]
if len(xs) > 1:
    assert rows[0] == 0
