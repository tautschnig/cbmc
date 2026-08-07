# twin: a misspelled key raises KeyError; the dict model must not
# prove through a wrong-key read.
from typing import TypedDict


class Cfg(TypedDict):
    name: str
    port: int


def use(c: Cfg) -> int:
    return c['prot']


cfg: Cfg = {'name': 'svc', 'port': 8080}
assert use(cfg) == 8080
