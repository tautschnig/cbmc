# Twin of typeddict-stub-return: subscripting an UNDECLARED key on a
# TypedDict-annotated stub's return raises KeyError — the misspelled
# response-key bug must be a violation, never a silent proof.
from typing import TypedDict


class CreateStageResponse(TypedDict):
    StageName: str
    ApiId: str


def create_stage(*, params: dict,
                 region_name=None) -> 'CreateStageResponse':
    assert "ApiId" in params
    ...


stage = create_stage(params={"ApiId": "a1"})
url = stage['InvokeUrl']
assert url is not None
