# Stale-copy soundness: the memoised SoA materialisation must be
# invalidated by an in-place mutation -- len(apps) == n after an
# append was a demonstrated false proof before the statement-level
# invalidation.
from typing import TypedDict, List


class App(TypedDict):
    appId: int


class Resp(TypedDict):
    apps: List[App]


def list_apps() -> Resp: ...


resp = list_apps()
apps = resp['apps']
n = len(apps)
apps.append({'appId': 7})
assert len(apps) == n
