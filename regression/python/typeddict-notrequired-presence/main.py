# PEP 589 requiredness in stub-returned TypedDicts: REQUIRED keys are
# always present (reads safe); OPTIONAL keys (NotRequired / a
# total=False class) are present in an arbitrary SUBSET, so guarded
# reads prove. (perf-study t5/t6)
from typing import TypedDict, NotRequired


class Cfg(TypedDict):
    name: str
    retries: NotRequired[int]


class Opts(TypedDict, total=False):
    debug: bool


def get_cfg() -> Cfg: ...


def get_opts() -> Opts: ...


c = get_cfg()
s = c['name']            # required: no KeyError
assert s == s
if 'retries' in c:
    r = c['retries']     # guarded optional: safe
    assert r == r
o = get_opts()
if 'debug' in o:
    d = o['debug']
    assert d == d
