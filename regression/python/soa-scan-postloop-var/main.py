from typing import TypedDict, List


class R(TypedDict):
    v: int


def fetch() -> List[R]: ...


def probe() -> None:
    xs = fetch()
    for r in xs:
        if r['v'] > 999999:
            return
    if len(xs) > 0:
        # PLR 8.3: r is the LAST element on the no-match path.
        assert r['v'] == xs[len(xs) - 1]['v']
        assert r['v'] <= 999999


probe()
