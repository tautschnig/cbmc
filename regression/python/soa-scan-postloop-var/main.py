from typing import TypedDict, List


class R(TypedDict):
    v: int


def fetch() -> List[R]: ...


def probe() -> None:
    xs = fetch()
    for r in xs:
        if r['v'] > 999999:
            return
    # r is now bound to the scan WITNESS INDEX (an i64) -- a bare
    # read here must not pun it into a value. In CPython r is the
    # LAST ELEMENT (a dict) if xs non-empty, else unbound.
    if len(xs) > 0:
        x = r['v']
        assert x == x


probe()
