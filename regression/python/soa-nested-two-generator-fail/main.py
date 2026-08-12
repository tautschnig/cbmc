from typing import TypedDict, List


class Node(TypedDict):
    weight: int


class Cluster(TypedDict):
    size: int
    nodes: List[Node]


def fetch() -> List[Cluster]: ...


cs = fetch()
loads = [c['size'] + n['weight'] for c in cs if c['size'] > 50 for n in c['nodes'] if n['weight'] > 10]
# filters: size >= 51, weight >= 11 -> every element >= 62
for x in loads:
    assert x >= 63    # MUST NOT VERIFY (62 possible)
