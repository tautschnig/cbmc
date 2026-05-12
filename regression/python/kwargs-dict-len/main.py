from typing import Dict, TypedDict, Unpack, Required


class InputDict(TypedDict):
    Tags: Required[Dict[str, str]]


def count_tags(**kwargs: Unpack[InputDict]) -> int:
    return len(kwargs["Tags"])


tags: Dict[str, str] = {"a": "1", "b": "2", "c": "3"}
r = count_tags(Tags=tags)
assert r == 3
