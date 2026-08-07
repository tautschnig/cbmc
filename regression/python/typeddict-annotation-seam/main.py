# PEP 589: a TypedDict is a plain dict at runtime. The annotation
# must resolve to the dict layout at EVERY position -- module-level
# globals (registered in the early pass, before class bodies
# convert), parameters, and locals -- not only the specially-cased
# bare return slot. Previously the class-STRUCT resolution dropped
# the dict literal (coerced to nondet) and mismodelled subscripts.
from typing import TypedDict


class Cfg(TypedDict):
    name: str
    port: int


def use(c: Cfg) -> int:
    return c['port']


cfg: Cfg = {'name': 'svc', 'port': 8080}
assert use(cfg) == 8080
local: Cfg = {'name': 'x', 'port': 1}
assert local['port'] == 1
