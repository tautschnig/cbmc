from typing import TypedDict, List


class R(TypedDict):
    v: int


def fetch() -> List[R]: ...


rs = fetch()
i = 42                      # user variable named like the binder
assume(all(rs[i]['v'] >= 0 for i in range(len(rs))))
assert i == 42              # PLR 6.2.4: comprehension scope does not leak
