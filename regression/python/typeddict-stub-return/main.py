# PEP 589: a TypedDict is a plain dict at runtime. A body-less stub
# (the SDK interface-declaration shape: `assert ...; ...`) annotated
# to return a TypedDict — directly or via a PEP 484 forward-ref
# string — returns a dict with EXACTLY the declared keys and nondet
# values: declared-key reads are unconstrained (no false alarm),
# .get() of an undeclared key is None, and a function with a REAL
# body returning a dict literal coerces into the same canonical
# layout. The misspelled-key KeyError is pinned by the -fail twin.
# (Reported: the stub's return was an unconstrained value, so
# stage['InvokeUrl'] on a response lacking that key verified
# silently.)
from typing import TypedDict


class CreateStageResponse(TypedDict):
    StageName: str
    ApiId: str


def create_stage(*, params: dict,
                 region_name=None) -> 'CreateStageResponse':
    assert "ApiId" in params
    ...


class Counted(TypedDict):
    Name: str
    Count: int


def make() -> Counted:
    return {'Name': 'x', 'Count': 3}


stage = create_stage(params={"ApiId": "a1"})
name = stage['StageName']
assert name is None or name is not None
assert stage.get('InvokeUrl') is None

r = make()
assert r['Name'] == 'x'
assert r['Count'] == 3
