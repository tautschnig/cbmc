# twin (perf-study t5): an UNGUARDED read of a NotRequired key must
# carry its KeyError obligation -- the key may be absent.
from typing import TypedDict, NotRequired


class Cfg(TypedDict):
    name: str
    retries: NotRequired[int]


def get_cfg() -> Cfg: ...


c = get_cfg()
r = c['retries']
assert r == r
