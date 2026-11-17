# dataclasses.field: default, default_factory (a new object per instance)
# and init=False.
import dataclasses
from dataclasses import dataclass, field
from typing import List


def seed() -> List[int]:
    return [5]


@dataclass
class Bin:
    label: str
    items: List[int] = dataclasses.field(default_factory=list)
    extra: List[int] = field(default_factory=seed)
    limit: int = field(default=4)
    used: int = field(default=0, init=False)


a = Bin("a")
b = Bin("b", [1, 2], limit=9)
a.items.append(7)
assert a.items == [7]
assert b.items == [1, 2]
assert a.extra == [5] and b.extra == [5]
assert a.limit == 4 and b.limit == 9
assert a.used == 0
c = Bin("c")
assert len(c.items) == 1
