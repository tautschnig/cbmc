from typing import TypedDict, List


class Node(TypedDict):
    weight: int


class Cluster(TypedDict):
    size: int
    nodes: List[Node]


def fetch() -> List[Cluster]: ...


cs = fetch()
loads = [n["weight"] for c in cs if c['size'] > 50 for n in c['nodes'] if n['weight'] > 10]
# filters: size >= 51, weight >= 11 -> every element >= 62
for x in loads:
    assert x >= 11

# The study ex2 shape (shape fact; arithmetic bodies are exact
# modulo the bounded-int wraparound model, see the twin).
def shape() -> None:
    cs2 = fetch()
    loads2 = [c['size'] + n['weight']
              for c in cs2 if c['size'] > 50
              for n in c['nodes'] if n['weight'] > 10]
    assert len(loads2) >= 0


shape()
